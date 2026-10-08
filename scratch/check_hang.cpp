#include <iostream>
#include <cassert>
#include "tray/include/tray_app.h"

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    std::cout << "Step 1: starting" << std::endl;
    
    badbt::TrayConfig config;
    config.start_minimized = true;
    badbt::TrayApp app(config);
    
    std::cout << "Step 2: initializing app" << std::endl;
    bool ok = app.initialize();
    std::cout << "Step 2 result: " << ok << std::endl;
    
    std::cout << "Step 3: shutting down app" << std::endl;
    app.shutdown();
    std::cout << "Step 3: shutdown done!" << std::endl;

    std::cout << "Step 4: testing human replay" << std::endl;
    badbt::TrayConfig cfg_human;
    cfg_human.replay_file = "data/raw/adhi_01.jsonl";
    cfg_human.start_minimized = true;
    badbt::TrayApp app_human(cfg_human);
    std::cout << "Step 4.1: initializing app_human" << std::endl;
    ok = app_human.initialize();
    std::cout << "Step 4.1 result: " << ok << std::endl;
    
    int count = 0;
    while (app_human.total_windows() < 669 && count < 200) {
        Sleep(50);
        count++;
        if (count % 20 == 0) {
            std::cout << "Windows so far: " << app_human.total_windows() << std::endl;
        }
    }
    std::cout << "Step 4.2: windows=" << app_human.total_windows() << std::endl;
    app_human.shutdown();
    std::cout << "Step 4.3: done human replay" << std::endl;
    return 0;
}
