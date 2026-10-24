#include "GameSpyClientMgr.h"
#include "platform/system.h"
#include <iostream>
#include <stdexcept>
#include <string>
void GameSpyClient_SetMasterServer(const char*);
static void require(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
int main(int argc,char** argv) {
    try {
        require(argc==2,"missing loopback master address");
        CGameSpyClientMgr browser;
        require(browser.Init("nolf","Jn3Ab4"),"browser initialization failed");
        GameSpyClient_SetMasterServer(argv[1]);
        require(browser.RefreshServers(LTTRUE),"browser refresh failed");
        const auto deadline=nolf::platform::Clock::now()+std::chrono::seconds(8);
        bool observed_active=false;
        while (browser.GetState()!=sl_idle && nolf::platform::Clock::now()<deadline) {
            observed_active=true; browser.Update();
            nolf::platform::sleep_for(std::chrono::milliseconds(1));
        }
        require(observed_active && browser.GetState()==sl_idle,"asynchronous browser did not finish");
        if (browser.GetProgress()!=100 || browser.GetNumServers()!=1) throw std::runtime_error("browser progress="+std::to_string(browser.GetProgress())+" listed="+std::to_string(browser.GetNumServers()));
        auto* server=browser.GetServer(0);
        require(server && std::string(server->GetName())=="Native NOLF" && std::string(server->GetAddress())=="127.0.0.1","binary master address or reordered status data was lost");
        require(server->GetNumPlayers()==1 && server->UpdatePlayers()==1,"player list was not decoded");
        require(std::string(server->GetPlayer(0)->GetName())=="Cate Archer","player status data was lost");
        require(browser.ExistNewServer(),"new-listing notification missing");
        browser.ClearServers(); require(browser.GetNumServers()==0,"refresh retained old server handles");
        GameSpyClient_SetMasterServer("none"); require(browser.RefreshServers() && browser.GetState()==sl_idle,"disabled master did not remain idle");
        GameSpyClient_SetMasterServer("127.0.0.1:bad"); require(!browser.RefreshServers(),"malformed master endpoint accepted");
        browser.Term(); browser.Term();
        std::cout << "Native master/status browser checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
