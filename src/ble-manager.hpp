#pragma once
#include <string>
#include <vector>
#include <functional>
#include <memory>

struct BleDevice {
    std::string id;
    std::string name;
    uint64_t bluetooth_address;
};

using HeartRateCallback = std::function<void(int heart_rate)>;
using ScanCallback = std::function<void(const BleDevice& device)>;

class BleManager {
public:
    virtual ~BleManager() = default;
    
    virtual void StartScan(ScanCallback callback) = 0;
    virtual void StopScan() = 0;
    virtual void Connect(const std::string& device_id) = 0;
    virtual void Disconnect() = 0;
    virtual void SetHeartRateCallback(HeartRateCallback callback) = 0;
    virtual bool IsConnected() const = 0;
    
    static std::shared_ptr<BleManager> Create();
};
