/*
    This file is part of kawpowminer.

    kawpowminer is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    kawpowminer is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with kawpowminer.  If not, see <http://www.gnu.org/licenses/>.
*/

//#include <CLI/CLI.hpp>

#include "buildinfo.h"
#include <condition_variable>

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#include <iostream>
#include <queue>
#include <boost/bind.hpp>
#include <map>

#include "client.h"
#include "structs.h"
#if API_CORE
#include <libapicore/ApiServer.h>
#include <regex>
#endif

#if defined(__linux__) || defined(__APPLE__)
#include <execinfo.h>
#elif defined(_WIN32)
#include <Windows.h>
#endif

using namespace std;
using namespace dev;


// Global vars
bool g_running = false;
bool g_exitOnError = false;  // Whether or not kawpowminer should exit on mining threads errors
string wallet, rig, poolDomain;
int poolPort = 0;
condition_variable g_shouldstop;




struct MiningChannel : public LogChannel
{
    static const char* name() { return EthGreen " m"; }
    static const int verbosity = 2;
};

#define minelog clog(MiningChannel)

#if ETH_DBUS
#include <kawpowminer/DBusInt.h>
#endif

bool validateArgs(int argc, char** argv)
{
    if (argv[0] != "-P" || argc != 2)
        return false;

    vector<string> vs;
    boost::split(vs, argv[1], boost::is_any_of("//"));

    // the data of wallet, rig and pool
    vector<string> sv;
    boost::split(sv, vs[1], boost::is_any_of("@"));

    vector<string> clientData;
    boost::split(clientData, sv[0], boost::is_any_of("."));
    wallet = clientData[0];
    rig = clientData[1];

    vector<string> poolData;
    boost::split(poolData, sv[1], boost::is_any_of(":"));
    poolDomain = poolData[0];
    poolPort = stoi(poolData[1]);

    return true;

}

int main(int argc, char** argv)
{
    try
    {
        if (!validateArgs(argc, argv))
        {
            throw exception("Wrong Argumets!!!");
        }

        hostent* ip = gethostbyname(poolDomain.c_str());
        Client c(wallet ,rig);
        c.connectToServer(ip->h_addr_list[0], poolPort);
        c.startConversation();
    }
    catch (const std::exception& e)
    {
        cout << e.what() << endl;
    }
    return 0;
}
