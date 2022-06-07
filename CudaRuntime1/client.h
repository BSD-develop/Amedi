#pragma once

#include "json.h"
#include <WinSock2.h>
#include <Windows.h>
#include <string>
#include <bitset>
#include <cuda.h>
#include "structs.h"
#include "CUDAmedi_cuda.h"
#include <boost/format.hpp>
#include <boost/thread.hpp>
#include <boost/asio.hpp>

using WorkReceived = function<void(WorkPackage const&)>;

struct Session
{
    // Tstamp of sessio start
    chrono::steady_clock::time_point start = chrono::steady_clock::now();
    // Whether or not worker is subscribed
    atomic<bool> subscribed = { false };
    // Whether or not worker is authorized
    atomic<bool> authorized = { false };
    // Total duration of session in minutes
    unsigned long duration()
    {
        return (chrono::duration_cast<chrono::minutes>(chrono::steady_clock::now() - start))
            .count();
    }

    // EthereumStratum (1 and 2)

    // Extranonce currently active
    uint64_t extraNonce = 0;
    // Length of extranonce in bytes
    unsigned int extraNonceSizeBytes = 0;
    // Next work target
    dev::h256 nextWorkBoundary =
        dev::h256("0x00000000ffff0000000000000000000000000000000000000000000000000000");

    // EthereumStratum (2 only)
    bool firstMiningSet = false;
    unsigned int timeout = 30;  // Default to 30 seconds
    string sessionId = "";
    string workerId = "";
    string algo = "ethash";
    unsigned int epoch = 0;
    chrono::steady_clock::time_point lastTxStamp = chrono::steady_clock::now();

};


using namespace std;

class Client
{
public:
	Client(string wallet, string rig);
	~Client();
	void connectToServer(std::string serverIP, int port);
	void startConversation();
    void onWorkReceived(WorkReceived const& _handler) { m_onWorkReceived = _handler; }
    //void sendJ(Json::Value const& jReq);
    bool isConnected() { return m_connected.load(memory_order_relaxed); }

    //void asyncCompile();
    void kick_miner();
    void pause(MinerPauseEnum what);
    void resume(MinerPauseEnum fromwhat);
    bool initEpoch_internal();
    bool initEpoch();
    void search(uint8_t const* header, uint64_t target, uint64_t start_nonce, const WorkPackage& w);
    void workLoop();
private:
    string m_wallet;
    string m_rig;

    Solution _s;
    static unsigned s_minersCount;   // Total Number of Miners
    static unsigned s_dagLoadMode;   // Way dag should be loaded
    static unsigned s_dagLoadIndex;

    // amedi.h
    WorkPackage m_work;
    bitset<MinerPauseEnum::Pause_MAX> m_pauseFlags;
    DeviceDescriptor m_deviceDescriptor;
    EpochContext m_epochContext;

    // cudaAmedi.h
    uint64_t m_allocated_memory_dag = 0;
    size_t m_allocated_memory_light_cache = 0;
    atomic<bool> m_new_work = { false };
    hash64_t* m_device_dag = nullptr;
    hash64_t* m_device_light = nullptr;

    unsigned m_solution_submitted_max_id;
	WSADATA wasd;
	SOCKET _clientSocket;
	string startJson();
	string authorizeJson();
	void proccessResponse(Json::Value& res);
    string sumbitSolution();

    std::atomic<bool> m_connected = { false };
    Json::StreamWriterBuilder m_jSwBuilder;
    WorkReceived m_onWorkReceived;
	
    bool m_newjobprocessed = false;
    boost::asio::deadline_timer m_workloop_timer;
    boost::asio::io_service::strand m_io_strand;

    CUfunction m_kernel[2];
    std::vector<volatile Search_results*> m_search_buf;
    std::vector<cudaStream_t> m_streams;

    mutable boost::mutex x_work;
    mutable boost::mutex x_pause;
    boost::condition_variable m_new_work_signal;
    boost::condition_variable m_dag_loaded_signal;

    boost::thread* m_compileThread = nullptr;
    uint64_t m_nextProgpowPeriod = 0;
    const unsigned m_index = 0;
    uint8_t m_kernelCompIx = 0;
    uint8_t m_kernelExecIx = 1;
    const uint32_t m_batch_size;
    const uint32_t m_streams_batch_size;
    uint64_t m_current_target = 0;
	WorkPackage m_current;
    unique_ptr<Session> m_session;
    std::chrono::time_point<std::chrono::steady_clock> m_current_timestamp;
};

std::string processError(Json::Value& responseObject);
static string getResString(char* buff);
void set_constants(hash64_t* _dag, uint32_t _dag_size, hash64_t* _light, uint32_t _light_size);