#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "ble-manager.hpp"
#include <obs-module.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Storage.Streams.h>

#include <iostream>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <vector>
#include <atomic>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::Advertisement;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Storage::Streams;

// Helper to check for valid UTF-8
static bool IsValidUtf8(const std::vector<uint8_t>& data) {
    int n;
    for (size_t i = 0; i < data.size(); ++i) {
        if ((data[i] & 0x80) == 0) {
            n = 0;
        } else if ((data[i] & 0xE0) == 0xC0) {
            n = 1;
        } else if ((data[i] & 0xF0) == 0xE0) {
            n = 2;
        } else if ((data[i] & 0xF8) == 0xF0) {
            n = 3;
        } else {
            return false;
        }
        for (int j = 0; j < n; ++j) {
            if (++i == data.size() || (data[i] & 0xC0) != 0x80) {
                return false;
            }
        }
    }
    return true;
}

// Helper to convert Bytes (likely GBK) to UTF-8
static std::string BytesToUtf8(const std::vector<uint8_t>& bytes, UINT codepage) {
    if (bytes.empty()) return "";
    int len = MultiByteToWideChar(codepage, 0, (LPCSTR)bytes.data(), (int)bytes.size(), NULL, 0);
    if (len <= 0) return "";
    std::wstring wstr(len, 0);
    MultiByteToWideChar(codepage, 0, (LPCSTR)bytes.data(), (int)bytes.size(), &wstr[0], len);
    return to_string(wstr);
}

// Helper to trim trailing garbage (0x00, 0xFF) and non-printable
static void TrimRawBytes(std::vector<uint8_t>& bytes) {
    while (!bytes.empty() && (bytes.back() == 0x00 || bytes.back() == 0xFF)) {
        bytes.pop_back();
    }
}

// UUIDs
// 0000180d-0000-1000-8000-00805f9b34fb
static const guid HR_SERVICE_UUID = { 0x0000180d, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb } };
// 00002a37-0000-1000-8000-00805f9b34fb
static const guid HR_MEASUREMENT_CHAR_UUID = { 0x00002a37, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb } };

class BleManagerWinRT : public BleManager, public std::enable_shared_from_this<BleManagerWinRT> {
    BluetoothLEAdvertisementWatcher watcher_{ nullptr };
    BluetoothLEDevice device_{ nullptr };
    GattCharacteristic hr_characteristic_{ nullptr };
    event_token value_changed_token_;
    event_token connection_status_token_;
    
    ScanCallback scan_callback_;
    HeartRateCallback hr_callback_;
    
    std::mutex mutex_;
    bool is_scanning_ = false;
    
    // Auto-reconnect logic
    bool should_reconnect_ = false;
    uint64_t target_address_ = 0;
    std::thread reconnect_thread_;
    std::atomic<bool> reconnect_thread_active_{false};
    std::atomic<bool> is_connecting_{false};

public:
    BleManagerWinRT() {
        watcher_ = BluetoothLEAdvertisementWatcher();
        watcher_.ScanningMode(BluetoothLEScanningMode::Active);
        
        watcher_.Received([this](BluetoothLEAdvertisementWatcher sender, BluetoothLEAdvertisementReceivedEventArgs args) {
            // Check for Heart Rate Service
            bool has_hr_service = false;
            for (auto uuid : args.Advertisement().ServiceUuids()) {
                if (uuid == HR_SERVICE_UUID) {
                    has_hr_service = true;
                    break;
                }
            }
            
            // We can also check Company ID if needed for filtering specific bands, 
            // but standard service is the requirement for 10 series.
            
            if (has_hr_service) {
                 BleDevice dev;
                 dev.bluetooth_address = args.BluetoothAddress();
                 dev.id = std::to_string(dev.bluetooth_address);
                 
                 // Try to decode name manually to handle GBK/garbled text
                 std::string name;
                 for (auto section : args.Advertisement().DataSections()) {
                     uint8_t type = section.DataType();
                     if (type == 0x09 || type == 0x08) { // Complete or Shortened Local Name
                         auto reader = DataReader::FromBuffer(section.Data());
                         std::vector<uint8_t> bytes;
                         while (reader.UnconsumedBufferLength() > 0) {
                             bytes.push_back(reader.ReadByte());
                         }
                         
                         TrimRawBytes(bytes);

                         if (!bytes.empty()) {
                             if (IsValidUtf8(bytes)) {
                                 std::string s(bytes.begin(), bytes.end());
                                 if (name.empty() || type == 0x09) name = s; 
                             } else {
                                 // Try GBK (936)
                                 std::string s = BytesToUtf8(bytes, 936);
                                 if (!s.empty()) {
                                     if (name.empty() || type == 0x09) name = s;
                                 }
                             }
                         }
                     }
                 }

                 if (!name.empty()) {
                     dev.name = name;
                 } else {
                     dev.name = to_string(args.Advertisement().LocalName());
                 }
                 
                 if (dev.name.empty()) {
                     dev.name = "Unknown Device (" + dev.id + ")";
                 }

                 std::lock_guard<std::mutex> lock(mutex_);
                 if (scan_callback_) {
                     scan_callback_(dev);
                 }
            }
        });
    }
    
    ~BleManagerWinRT() {
        StopScan();
        Disconnect();
        StopReconnectThread();
    }

    void StopReconnectThread() {
        reconnect_thread_active_ = false;
        if (reconnect_thread_.joinable()) {
            reconnect_thread_.join();
        }
    }

    void StartReconnectThread() {
        if (reconnect_thread_active_) return;
        reconnect_thread_active_ = true;
        reconnect_thread_ = std::thread([this]() {
            while (reconnect_thread_active_) {
                if (should_reconnect_ && target_address_ != 0) {
                     // Check if connected
                     bool connected = false;
                     {
                         std::lock_guard<std::mutex> lock(mutex_);
                         if (device_) {
                             try {
                                 connected = (device_.ConnectionStatus() == BluetoothConnectionStatus::Connected);
                             } catch(...) {}
                         }
                     }
                     
                     if (!connected && !is_connecting_) {
                         blog(LOG_INFO, "Auto-reconnect: Attempting to connect...");
                         ConnectAsync(target_address_);
                     }
                }
                
                // Sleep for 5s
                for (int i = 0; i < 50; i++) {
                    if (!reconnect_thread_active_) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        });
    }

    void StartScan(ScanCallback callback) override {
        std::lock_guard<std::mutex> lock(mutex_);
        scan_callback_ = callback;
        if (!is_scanning_) {
            try {
                watcher_.Start();
                is_scanning_ = true;
            } catch(...) {
                // Handle start failure
            }
        }
    }

    void StopScan() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (is_scanning_) {
            watcher_.Stop();
            is_scanning_ = false;
        }
    }

    void Connect(const std::string& device_id_str) override {
        try {
            uint64_t address = std::stoull(device_id_str);
            
            // Set up auto-reconnect
            {
                std::lock_guard<std::mutex> lock(mutex_);
                should_reconnect_ = true;
                target_address_ = address;
            }
            
            ConnectAsync(address);
            StartReconnectThread();
        } catch (...) {
            // Invalid ID
        }
    }
    
    fire_and_forget ConnectAsync(uint64_t address) {
        // Keep alive while async
        auto self = shared_from_this();

        bool expected = false;
        if (!is_connecting_.compare_exchange_strong(expected, true)) {
            blog(LOG_INFO, "Connection already in progress, skipping request for %llu", address);
            co_return;
        }

        struct ConnectionGuard {
            std::atomic<bool>& flag;
            ConnectionGuard(std::atomic<bool>& f) : flag(f) {}
            ~ConnectionGuard() { flag = false; }
        };
        ConnectionGuard guard(is_connecting_);
        
        try {
            blog(LOG_INFO, "Connecting to device address: %llu", address);
            auto device = co_await BluetoothLEDevice::FromBluetoothAddressAsync(address);
            if (!device) {
                blog(LOG_WARNING, "Failed to get device object");
                co_return;
            }
            
            {
                std::lock_guard<std::mutex> lock(mutex_);
                device_ = device;
            }

            blog(LOG_INFO, "Discovering services...");
            auto servicesResult = co_await device.GetGattServicesForUuidAsync(HR_SERVICE_UUID);
            if (servicesResult.Status() != GattCommunicationStatus::Success) {
                blog(LOG_WARNING, "Failed to get services. Status: %d", (int)servicesResult.Status());
                co_return;
            }
            
            auto services = servicesResult.Services();
            if (services.Size() == 0) {
                 blog(LOG_WARNING, "No Heart Rate Service found");
                 co_return;
            }
            
            auto service = services.GetAt(0);
            blog(LOG_INFO, "Heart Rate Service found");
            
            auto charResult = co_await service.GetCharacteristicsForUuidAsync(HR_MEASUREMENT_CHAR_UUID);
            if (charResult.Status() != GattCommunicationStatus::Success) {
                 blog(LOG_WARNING, "Failed to get characteristics");
                 co_return;
            }
            
            auto chars = charResult.Characteristics();
            if (chars.Size() == 0) {
                 blog(LOG_WARNING, "No Heart Rate Measurement Characteristic found");
                 co_return;
            }
            
            GattCharacteristic characteristic = chars.GetAt(0);
            
            // Check properties
            auto properties = characteristic.CharacteristicProperties();
            if ((properties & GattCharacteristicProperties::Notify) == GattCharacteristicProperties::None) {
                 blog(LOG_WARNING, "Characteristic does not support Notify");
                 co_return;
            }
            
            {
                std::lock_guard<std::mutex> lock(mutex_);
                hr_characteristic_ = characteristic;
            }

            // Subscribe
            blog(LOG_INFO, "Subscribing to notifications...");
            auto status = co_await characteristic.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue::Notify);
            if (status == GattCommunicationStatus::Success) {
                 blog(LOG_INFO, "Subscribed successfully");
                 std::lock_guard<std::mutex> lock(mutex_);
                 value_changed_token_ = characteristic.ValueChanged({ this, &BleManagerWinRT::OnValueChanged });
                 
                 // If success, we should try to read once if possible or wait for notify.
                 // But HR measurement is usually Notify only.
            } else {
                 blog(LOG_WARNING, "Failed to subscribe. Status: %d", (int)status);
            }
        } catch (...) {
            blog(LOG_ERROR, "Exception in ConnectAsync");
            Disconnect();
        }
    }

    void Disconnect() override {
        // User explicitly called disconnect, so disable auto-reconnect
        {
            std::lock_guard<std::mutex> lock(mutex_);
            should_reconnect_ = false;
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        if (hr_characteristic_) {
            if (value_changed_token_.value != 0) {
                 try {
                    hr_characteristic_.ValueChanged(value_changed_token_);
                 } catch(...) {}
                 value_changed_token_ = {};
            }
            
            // Try to write None to CCCD to stop notifications on the device
            try {
                auto op = hr_characteristic_.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue::None);
                // Simple block wait with timeout to avoid hanging UI/Shutdown
                if (op.wait_for(std::chrono::seconds(1)) != AsyncStatus::Completed) {
                    op.Cancel();
                }
            } catch (...) {
                // Ignore errors during disconnect
            }

            hr_characteristic_ = nullptr;
        }
        
        if (device_) {
            // Unsubscribe connection status
            if (connection_status_token_.value != 0) {
                try {
                    device_.ConnectionStatusChanged(connection_status_token_);
                } catch(...) {}
                connection_status_token_ = {};
            }
            
            device_.Close();
            device_ = nullptr;
        }
    }

    void SetHeartRateCallback(HeartRateCallback callback) override {
        std::lock_guard<std::mutex> lock(mutex_);
        hr_callback_ = callback;
    }
    
    bool IsConnected() const override {
        // Simple check
        if (!device_) return false;
        try {
            return device_.ConnectionStatus() == BluetoothConnectionStatus::Connected;
        } catch(...) {
            return false;
        }
    }

private:
    void OnValueChanged(GattCharacteristic const&, GattValueChangedEventArgs const& args) {
        auto reader = DataReader::FromBuffer(args.CharacteristicValue());
        reader.ByteOrder(ByteOrder::LittleEndian); // Important!
        
        if (reader.UnconsumedBufferLength() == 0) return;
        
        uint8_t flags = reader.ReadByte();
        bool is_u16 = flags & 0x01;
        
        int hr = 0;
        if (is_u16) {
             if (reader.UnconsumedBufferLength() >= 2)
                hr = reader.ReadUInt16();
        } else {
             if (reader.UnconsumedBufferLength() >= 1)
                hr = reader.ReadByte();
        }
        
        // Debug output
        // std::stringstream ss;
        // ss << "HR: " << hr;
        // OutputDebugStringA(ss.str().c_str());

        std::lock_guard<std::mutex> lock(mutex_);
        if (hr_callback_) {
            hr_callback_(hr);
        }
    }
};

std::shared_ptr<BleManager> BleManager::Create() {
    return std::make_shared<BleManagerWinRT>();
}
