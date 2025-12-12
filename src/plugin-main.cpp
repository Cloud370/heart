#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/platform.h>
#include <util/dstr.h>
#include "ble-manager.hpp"
#include <windows.h>
#include <shellapi.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include "httplib.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("miband-heart-rate", "en-US")

// Globals
static std::shared_ptr<BleManager> g_ble;
static std::unique_ptr<httplib::Server> g_server;
static std::thread g_server_thread;
static std::atomic<int> g_latest_hr{-1};
static std::string g_web_dir;
static std::string g_theme = "default";
static std::string g_last_device_id = "";
static std::string g_config_path;

static std::mutex g_scan_mutex;
static std::vector<BleDevice> g_found_devices;
static bool g_scanning = false;

static int g_server_port = 0;

static void save_config();

static void load_config() {
    char* path = obs_module_config_path("config.json");
    if (path) {
        g_config_path = path;
        bfree(path);
        
        blog(LOG_INFO, "Loading config from: %s", g_config_path.c_str());
        
        obs_data_t *data = obs_data_create_from_json_file(g_config_path.c_str());
        if (data) {
            const char* theme = obs_data_get_string(data, "theme");
            const char* device_id = obs_data_get_string(data, "last_device_id");
            
            if (theme && *theme) g_theme = theme;
            if (device_id && *device_id) g_last_device_id = device_id;
            
            blog(LOG_INFO, "Config loaded - Theme: %s, Last Device: %s", g_theme.c_str(), g_last_device_id.c_str());
            obs_data_release(data);
        } else {
            blog(LOG_INFO, "Config file not found or invalid, creating new one.");
            save_config();
        }
    } else {
        blog(LOG_ERROR, "Failed to get module config path");
    }
}

static std::mutex g_config_mutex;

static void save_config() {
    std::lock_guard<std::mutex> lock(g_config_mutex);
    if (g_config_path.empty()) return;
    
    // Ensure directory exists
    std::string dir_path = g_config_path;
    size_t last_slash = dir_path.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        dir_path = dir_path.substr(0, last_slash);
        os_mkdirs(dir_path.c_str());
    }

    obs_data_t *data = obs_data_create();
    obs_data_set_string(data, "theme", g_theme.c_str());
    obs_data_set_string(data, "last_device_id", g_last_device_id.c_str());
    
    if (!obs_data_save_json_safe(data, g_config_path.c_str(), "tmp", "bak")) {
        blog(LOG_WARNING, "Failed to save config to %s", g_config_path.c_str());
    } else {
        blog(LOG_INFO, "Config saved to %s", g_config_path.c_str());
    }
    
    obs_data_release(data);
}

// Helper to find web directory
static void setup_web_dir() {
    char* path = obs_module_file("web");
    if (path) {
        g_web_dir = path;
        bfree(path);
        blog(LOG_INFO, "Web directory found: %s", g_web_dir.c_str());
    } else {
        blog(LOG_WARNING, "Could not find 'web' directory in plugin data path.");
    }
    
    load_config();
}

static void open_url(const char* url) {
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
}

static std::string get_base_url() {
    return "http://localhost:" + std::to_string(g_server_port);
}

static void check_and_create_source() {
    obs_source_t* scene_source = obs_frontend_get_current_scene();
    if (!scene_source) return;

    obs_scene_t* scene = obs_scene_from_source(scene_source);
    if (!scene) {
        obs_source_release(scene_source);
        return;
    }

    if (g_server_port == 0) {
        obs_source_release(scene_source);
        return;
    }

    const char* source_name = "心率显示 (Heart Rate)";
    obs_sceneitem_t* item = obs_scene_find_source_recursive(scene, source_name);
    std::string url = get_base_url() + "/index.html";

    if (item) {
        // Source exists, check and update URL if needed
        obs_source_t* source = obs_sceneitem_get_source(item);
        if (source) {
            obs_data_t* settings = obs_source_get_settings(source);
            const char* current_url = obs_data_get_string(settings, "url");
            if (current_url && url != current_url) {
                blog(LOG_INFO, "Updating Heart Rate browser source URL to %s", url.c_str());
                obs_data_set_string(settings, "url", url.c_str());
                obs_source_update(source, settings);
            }
            obs_data_release(settings);
        }
    } else {
        blog(LOG_INFO, "Creating Heart Rate browser source...");
        obs_data_t* settings = obs_data_create();
        obs_data_set_string(settings, "url", url.c_str());
        obs_data_set_int(settings, "width", 350);
        obs_data_set_int(settings, "height", 150);
        obs_data_set_int(settings, "fps", 60);
        obs_data_set_bool(settings, "is_local_file", false);
        
        obs_source_t* source = obs_source_create("browser_source", source_name, settings, nullptr);
        obs_data_release(settings);
        
        if (source) {
            obs_sceneitem_t* new_item = obs_scene_add(scene, source);
            if (new_item) {
                obs_sceneitem_set_visible(new_item, true);
            }
            obs_source_release(source);
        }
    }

    obs_source_release(scene_source);
}

static void on_frontend_event(enum obs_frontend_event event, void* data) {
    if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
        check_and_create_source();
        
        // Auto connect
        if (!g_last_device_id.empty() && g_ble) {
            blog(LOG_INFO, "Auto connecting to last device: %s", g_last_device_id.c_str());
            g_ble->Connect(g_last_device_id);
        }
    }
}

static void on_tool_config(void* data) {
    if (g_server_port > 0) {
        open_url((get_base_url() + "/settings.html").c_str());
    }
}

static void start_http_server() {
    g_server = std::make_unique<httplib::Server>();
    
    // Serve static files
    auto serve_file = [](const std::string& filename, const std::string& mime) {
        return [filename, mime](const httplib::Request&, httplib::Response& res) {
            std::string path = g_web_dir + "/" + filename;
            std::ifstream file(path);
            if (file) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                res.set_content(buffer.str(), mime);
            } else {
                res.status = 404;
                res.set_content("File not found", "text/plain");
            }
        };
    };

    g_server->Get("/", serve_file("index.html", "text/html"));
    g_server->Get("/index.html", serve_file("index.html", "text/html"));
    g_server->Get("/settings.html", serve_file("settings.html", "text/html"));
    g_server->Get("/style.css", serve_file("style.css", "text/css"));
    g_server->Get("/script.js", serve_file("script.js", "application/javascript"));

    // API: Heart Rate
    g_server->Get("/api/hr", [](const httplib::Request&, httplib::Response& res) {
        std::stringstream ss;
        int hr = -1;
        if (g_ble && g_ble->IsConnected()) {
            hr = g_latest_hr;
        }
        ss << "{\"hr\": " << hr << "}";
        res.set_content(ss.str(), "application/json");
        res.set_header("Access-Control-Allow-Origin", "*");
    });

    // API: Disconnect
    g_server->Post("/api/disconnect", [](const httplib::Request&, httplib::Response& res) {
        if (g_ble) {
            g_ble->Disconnect();
            g_latest_hr = -1;
            res.set_content("{\"status\": \"disconnected\"}", "application/json");
        } else {
            res.status = 500;
        }
    });

    // API: Reset
    g_server->Post("/api/reset", [](const httplib::Request&, httplib::Response& res) {
        if (g_ble) {
            g_ble->Disconnect();
        }
        g_latest_hr = -1;
        {
            std::lock_guard<std::mutex> lock(g_config_mutex);
            g_last_device_id = "";
        }
        save_config();
        res.set_content("{\"status\": \"reset\"}", "application/json");
    });

    // API: Get Theme
    g_server->Get("/api/theme", [](const httplib::Request&, httplib::Response& res) {
        std::string theme;
        {
            std::lock_guard<std::mutex> lock(g_config_mutex);
            theme = g_theme;
        }
        
        obs_data_t *data = obs_data_create();
        obs_data_set_string(data, "theme", theme.c_str());
        const char* json = obs_data_get_json(data);
        if (json) {
             res.set_content(json, "application/json");
        } else {
             res.status = 500;
        }
        obs_data_release(data);
    });

    // API: Set Theme
    g_server->Post("/api/theme", [](const httplib::Request& req, httplib::Response& res) {
        std::string body = req.body;
        blog(LOG_INFO, "API Set Theme Request Body: %s", body.c_str());
        
        std::string new_theme;
        obs_data_t *data = obs_data_create_from_json(body.c_str());
        if (data) {
            const char* val = obs_data_get_string(data, "theme");
            if (val) {
                 new_theme = val;
            }
            obs_data_release(data);
        }

        if (!new_theme.empty()) {
            blog(LOG_INFO, "Setting theme to: %s", new_theme.c_str());
            {
                std::lock_guard<std::mutex> lock(g_config_mutex);
                g_theme = new_theme;
            }
            // save_config locks internally, so we release our lock first
            save_config();
            res.set_content("{\"status\": \"ok\"}", "application/json");
        } else {
            blog(LOG_WARNING, "Failed to parse theme");
            res.status = 400;
        }
    });

    // API: Start Scan
    g_server->Post("/api/scan", [](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(g_scan_mutex);
        if (g_scanning) {
             res.set_content("{\"status\": \"scanning\"}", "application/json");
             return;
        }
        g_scanning = true;
        g_found_devices.clear();
        
        if (g_ble) {
            g_ble->StartScan([](const BleDevice& dev) {
                std::lock_guard<std::mutex> lock(g_scan_mutex);
                bool exists = false;
                for (auto& d : g_found_devices) {
                    if (d.id == dev.id) { 
                        // Update name if the new one is longer (likely more complete)
                        if (dev.name.length() > d.name.length()) {
                            d.name = dev.name;
                        }
                        exists = true; 
                        break; 
                    }
                }
                if (!exists) {
                    g_found_devices.push_back(dev);
                }
            });
            
            // Auto stop scan after 10s
            std::thread([]() {
                std::this_thread::sleep_for(std::chrono::seconds(10));
                std::lock_guard<std::mutex> lock(g_scan_mutex);
                if (g_scanning && g_ble) {
                    g_ble->StopScan();
                    g_scanning = false;
                }
            }).detach();
        }
        
        res.set_content("{\"status\": \"started\"}", "application/json");
    });

    // API: Get Devices
    g_server->Get("/api/devices", [](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(g_scan_mutex);
        std::stringstream ss;
        ss << "[";
        for (size_t i = 0; i < g_found_devices.size(); ++i) {
            if (i > 0) ss << ",";
            ss << "{\"name\": \"" << g_found_devices[i].name << "\", \"id\": \"" << g_found_devices[i].id << "\"}";
        }
        ss << "]";
        res.set_content(ss.str(), "application/json");
    });

    // API: Connect
    g_server->Post("/api/connect", [](const httplib::Request& req, httplib::Response& res) {
        // Simple JSON parse: {"id": "..."}
        std::string body = req.body;
        size_t id_pos = body.find("\"id\"");
        if (id_pos == std::string::npos) {
             res.status = 400;
             return;
        }
        size_t start = body.find("\"", id_pos + 4);
        if (start == std::string::npos) { res.status = 400; return; }
        start++;
        size_t end = body.find("\"", start);
        if (end == std::string::npos) { res.status = 400; return; }
        
        std::string id = body.substr(start, end - start);
        
        {
            std::lock_guard<std::mutex> lock(g_scan_mutex);
            if (g_scanning && g_ble) {
                g_ble->StopScan();
                g_scanning = false;
            }
        }
        
        if (g_ble) {
            g_ble->Connect(id);
            g_latest_hr = -1;
            {
                std::lock_guard<std::mutex> lock(g_config_mutex);
                g_last_device_id = id;
            }
            save_config();
            res.set_content("{\"status\": \"connecting\"}", "application/json");
        } else {
            res.status = 500;
        }
    });

    int port = 17878;
    while (port < 65535) {
        if (g_server->bind_to_port("0.0.0.0", port)) {
            g_server_port = port;
            blog(LOG_INFO, "HTTP Server started on port %d", g_server_port);
            g_server->listen_after_bind();
            return;
        }
        port++;
    }
    blog(LOG_ERROR, "Failed to bind to any port starting from 17878");
}

bool obs_module_load(void)
{
    setup_web_dir();

    // Init BLE
    g_ble = BleManager::Create();
    g_ble->SetHeartRateCallback([](int hr) {
        g_latest_hr = hr;
    });

    // Start Server
    g_server_thread = std::thread(start_http_server);
    g_server_thread.detach();

    // Wait for server to bind port (max 2 seconds)
    int retries = 0;
    while (g_server_port == 0 && retries < 20) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        retries++;
    }

    // Register UI
    obs_frontend_add_tools_menu_item("小米手环心率配置", on_tool_config, nullptr);
    obs_frontend_add_event_callback(on_frontend_event, nullptr);

    blog(LOG_INFO, "Heart Rate plugin loaded");
    return true;
}

void obs_module_unload(void)
{
    if (g_server) {
        g_server->stop();
    }
    if (g_ble) {
        g_ble->Disconnect();
        g_ble.reset();
    }
    blog(LOG_INFO, "Heart Rate plugin unloaded");
}
