#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include "client.h"
#include <exception>
#include <string>
#include <iostream>
#include <cstdlib>
#include <vector>
#include "json.hpp"
#include "arith_uint256.h"
#include "FixedHash.h"
#include <windows.data.json.h>
#include <boost/asio.hpp>
#include <boost/thread/win32/mutex.hpp>

boost::asio::io_service g_io_service;  // The IO service itself

using json = nlohmann::json;

unsigned Client::s_dagLoadMode = 0;
unsigned Client::s_dagLoadIndex = 0;
unsigned Client::s_minersCount = 0;

Client::Client(string wallet, string rig) : 
	m_workloop_timer(g_io_service), m_io_strand(g_io_service),
	m_batch_size(256* 512),
	m_streams_batch_size(256*512* 2)
{
	WSAStartup(MAKEWORD(2,2), &wasd);
	// we connect to server that uses TCP. thats why SOCK_STREAM & IPPROTO_TCP
	_clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	//if (_clientSocket == INVALID_SOCKET)
	//	throw std::exception(__FUNCTION__ " - socket");
	this->m_wallet = wallet;
	this->m_rig = rig;

}

Client::~Client()
{
	try
	{
		// the only use of the destructor should be for freeing 
		// resources that was allocated in the constructor
		closesocket(_clientSocket);
	}
	catch (...) {}
}


void Client::connectToServer(std::string serverIP, int port)
{

	struct sockaddr_in sa = { 0 };

	sa.sin_port = htons(port); // port that server will listen to
	sa.sin_family = AF_INET;   // must be AF_INET
	sa.sin_addr.s_addr = inet_addr(serverIP.c_str());    // the IP of the server

	// the process will not continue until the server accepts the client
	int status = connect(_clientSocket, (struct sockaddr*)&sa, sizeof(sa));

	int err = WSAGetLastError();
	if (status == INVALID_SOCKET)
		throw std::exception("Cant connect to server");
}

void Client::startConversation()
{
	/*
	*	send first json to the server - subscribe
	*/
	send(_clientSocket, startJson().c_str(), startJson().size(), 0);


	char m[1024];
	recv(_clientSocket, m, 1024, 0);
	m[1023] = 0;

	// get server response
	string serverRes = getResString(m);
	std::cout << "Message from server: " << serverRes << std::endl;



	/*
	*	send second json to server - authorize
	*/ 
	send(_clientSocket, authorizeJson().c_str(), authorizeJson().size(), 0);
	recv(_clientSocket, m, 1024, 0);
	m[1023] = 0;

	// get server response
	serverRes = getResString(m);
	std::cout << "Message from server: " << serverRes << std::endl;



	/*
	*	 get server response - set target
	*/
	recv(_clientSocket, m, 1024, 0);
	m[1023] = 0;

	// get server response
	serverRes = getResString(m);
	std::cout << "Message from server: " << serverRes << std::endl;

	// notify server
	send(_clientSocket, "\n", 1, 0);
	/*
	*	 get server response - notify
	*/
	recv(_clientSocket, m, 1024, 0);
	m[1023] = 0;

	// get server response
	serverRes = getResString(m);
	std::cout << "Message from server: " << serverRes << std::endl;

	m_newjobprocessed = false;
	std::string line;
	size_t offset = serverRes.find("\n");
	while (offset != string::npos)
	{
		if (offset > 0)
		{
			line = serverRes.substr(0, offset);
			boost::trim(line);

			if (!line.empty())
			{

				// Test validity of chunk and process
				Json::Value jMsg;
				Json::Reader jRdr;
				if (jRdr.parse(line, jMsg))
				{
					try
					{
						// Run in sync so no 2 different async reads may overlap
						proccessResponse(jMsg);
					}
					catch (const std::exception& _ex)
					{
						cout << "Stratum got invalid Json message : " << _ex.what();
					}
				}
				else
				{
					string what = jRdr.getFormattedErrorMessages();
					boost::replace_all(what, "\n", " ");
					cout << "Stratum got invalid Json message : " << what;
				}
			}
		}

		serverRes.erase(0, offset + 1);
		offset = serverRes.find("\n");
	}

	// There is a new job - dispatch it
	workLoop();

}

//void Client::asyncCompile()
//{
//	auto saveName = getThreadName();
//	setThreadName(name().c_str());
//
//	if (!dropThreadPriority())
//		cudalog << "Unable to lower compiler priority.";
//
//	cuCtxSetCurrent(m_context);
//
//	compileKernel(m_nextProgpowPeriod, m_epochContext.dagNumItems / 2, m_kernel[m_kernelCompIx]);
//
//	setThreadName(saveName.c_str());
//
//	m_kernelCompIx ^= 1;
//}

void Client::kick_miner()
{
	m_new_work.store(true, std::memory_order_relaxed);
	m_new_work_signal.notify_one();
}

void Client::pause(MinerPauseEnum what)
{
	boost::mutex::scoped_lock l(x_pause);
	m_pauseFlags.set(what);
	m_work.header = dev::h256();
	kick_miner();
}

void Client::resume(MinerPauseEnum fromwhat)
{
	boost::mutex::scoped_lock l(x_pause);
	m_pauseFlags.reset(fromwhat);
}

bool Client::initEpoch_internal()
{
	// If we get here it means epoch has changed so it's not necessary
	// to check again dag sizes. They're changed for sure
	bool retVar = false;
	m_current_target = 0;
	auto startInit = std::chrono::steady_clock::now();
	size_t RequiredMemory = (m_epochContext.dagSize + m_epochContext.lightSize);

	size_t FreeMemory = m_deviceDescriptor.freeMemory;
	FreeMemory += m_allocated_memory_dag;
	FreeMemory += m_allocated_memory_light_cache;

	// Release the pause flag if any
	resume(MinerPauseEnum::PauseDueToInsufficientMemory);
	resume(MinerPauseEnum::PauseDueToInitEpochError);

	if (FreeMemory < RequiredMemory)
	{
		cout << "Epoch " << m_epochContext.epochNumber << " requires "
			<< dev::getFormattedMemory((double)RequiredMemory) << " memory.";
		cout << "Only " << dev::getFormattedMemory((double)FreeMemory) << " available. Mining suspended on device ...";
		pause(MinerPauseEnum::PauseDueToInsufficientMemory);
		return true;  // This will prevent to exit the thread and
					  // Eventually resume mining when changing coin or epoch (NiceHash)
	}

	try
	{

		// If we have already enough memory allocated, we just have to
		// copy light_cache and regenerate the DAG
		if (m_allocated_memory_dag < m_epochContext.dagSize ||
			m_allocated_memory_light_cache < m_epochContext.lightSize)
		{
			// Release previously allocated memory for dag and light
			if (m_device_light) CUDA_SAFE_CALL(cudaFree(reinterpret_cast<void*>(m_device_light)));
			if (m_device_dag) CUDA_SAFE_CALL(cudaFree(reinterpret_cast<void*>(m_device_dag)));

			cout << "Generating DAG + Light : "
				<< dev::getFormattedMemory((double)RequiredMemory);

			// create buffer for cache
			CUDA_SAFE_CALL(cudaMalloc(reinterpret_cast<void**>(&m_device_light), m_epochContext.lightSize));
			m_allocated_memory_light_cache = m_epochContext.lightSize;
			CUDA_SAFE_CALL(cudaMalloc(reinterpret_cast<void**>(&m_device_dag), m_epochContext.dagSize));
			m_allocated_memory_dag = m_epochContext.dagSize;

		}
		else
		{
			cout << "Generating DAG + Light (reusing buffers): "
				<< dev::getFormattedMemory((double)RequiredMemory);
		}

		CUDA_SAFE_CALL(cudaMemcpy(reinterpret_cast<void*>(m_device_light), m_epochContext.lightCache,
			m_epochContext.lightSize, cudaMemcpyHostToDevice));

		set_constants(m_device_dag, m_epochContext.dagNumItems, m_device_light,
			m_epochContext.lightNumItems);  // in ethash_cuda_miner_kernel.cu

		ethash_generate_dag(
			m_device_dag, m_epochContext.dagSize, m_device_light, m_epochContext.lightNumItems, 256, 512, m_streams[0], m_deviceDescriptor.cuDeviceIndex);

		cout << "Generated DAG + Light in "
			<< std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - startInit)
			.count()
			<< " ms. "
			<< dev::getFormattedMemory((double)(m_deviceDescriptor.totalMemory - RequiredMemory))
			<< " left.";

		retVar = true;
	}
	catch (const cuda_runtime_error& ec)
	{
		cout << "Unexpected error " << ec.what() << " on CUDA device "
			<< m_deviceDescriptor.uniqueId;
		cout << "Mining suspended ...";
		pause(MinerPauseEnum::PauseDueToInitEpochError);
		retVar = true;
	}
	catch (std::runtime_error const& _e)
	{
		cout << "Fatal GPU error: " << _e.what();
		cout << "Terminating.";
		exit(-1);
	}

	return retVar;
}
bool Client::initEpoch()
{
	// When loading of DAG is sequential wait for
			// this instance to become current
	if (s_dagLoadMode == 1)
	{
		while (s_dagLoadIndex < m_index)
		{
			boost::system_time const timeout =
				boost::get_system_time() + boost::posix_time::seconds(3);
			boost::mutex::scoped_lock l(x_work);
			m_dag_loaded_signal.timed_wait(l, timeout);
		}
	}

	// Run the internal initialization
	// specific for miner
	bool result = initEpoch_internal();

	// Advance to next miner or reset to zero for 
	// next run if all have processed
	if (s_dagLoadMode == 1)
	{
		s_dagLoadIndex = (m_index + 1);
		if (s_minersCount == s_dagLoadIndex)
			s_dagLoadIndex = 0;
		else
			m_dag_loaded_signal.notify_all();
	}

}

void Client::search(uint8_t const* header, uint64_t target, uint64_t start_nonce, const WorkPackage& w)
{
	set_header(*reinterpret_cast<hash32_t const*>(header));
	if (m_current_target != target)
	{
		set_target(target);
		m_current_target = target;
	}
	hash32_t current_header = *reinterpret_cast<hash32_t const*>(header);
	hash64_t* dag;
	get_constants(&dag, NULL, NULL, NULL);

	auto search_start = std::chrono::steady_clock::now();

	// prime each stream, clear search result buffers and start the search
	uint32_t current_index;
	for (current_index = 0; current_index < 2;
		current_index++, start_nonce += m_batch_size)
	{
		cudaStream_t stream = m_streams[current_index];
		volatile Search_results& buffer(*m_search_buf[current_index]);
		buffer.count = 0;

		// Run the batch for this stream
		volatile Search_results* Buffer = &buffer;
		bool hack_false = false;
		void* args[] = { &start_nonce, &current_header, &m_current_target, &dag, &Buffer, &hack_false };
		CU_SAFE_CALL(cuLaunchKernel(m_kernel[m_kernelExecIx],  //
			256, 1, 1,                         // grid dim
			512, 1, 1,                        // block dim
			0,                                                 // shared mem
			stream,                                            // stream
			args, 0));                                         // arguments
	}

	// process stream batches until we get new work.
	bool done = false;

	uint32_t gids[MAX_SEARCH_RESULTS];
	dev::h256 mixHashes[MAX_SEARCH_RESULTS];


	while (!done)
	{
		// Exit next time around if there's new work awaiting
		bool t = true;
		

		// This inner loop will process each cuda stream individually
		for (current_index = 0; current_index < 2;
			current_index++, start_nonce += m_batch_size)
		{
			// Each pass of this loop will wait for a stream to exit,
			// save any found solutions, then restart the stream
			// on the next group of nonces.
			cudaStream_t stream = m_streams[current_index];

			// Wait for the stream complete
			CUDA_SAFE_CALL(cudaStreamSynchronize(stream));

			

			// Detect solutions in current stream's solution buffer
			volatile Search_results& buffer(*m_search_buf[current_index]);
			uint32_t found_count = min((unsigned)buffer.count, MAX_SEARCH_RESULTS);

			if (found_count)
			{
				buffer.count = 0;

				// Extract solution and pass to higer level
				// using io_service as dispatcher

				for (uint32_t i = 0; i < found_count; i++)
				{
					gids[i] = buffer.result[i].gid;
					memcpy(mixHashes[i].data(), (void*)&buffer.result[i].mix,
						sizeof(buffer.result[i].mix));
				}
			}

			// restart the stream on the next batch of nonces
			// unless we are done for this round.
			if (!done)
			{
				volatile Search_results* Buffer = &buffer;
				bool hack_false = false;
				void* args[] = { &start_nonce, &current_header, &m_current_target, &dag, &Buffer, &hack_false };
				CU_SAFE_CALL(cuLaunchKernel(m_kernel[m_kernelExecIx],  //
					256, 1, 1,                         // grid dim
					512, 1, 1,                        // block dim
					0,                                                 // shared mem
					stream,                                            // stream
					args, 0));                                         // arguments
			}
			if (found_count)
			{
				uint64_t nonce_base = start_nonce - m_streams_batch_size;
				for (uint32_t i = 0; i < found_count; i++)
				{
					uint64_t nonce = nonce_base + gids[i];

					Solution s;
					s.nonce = nonce;
					s.mixHash = mixHashes[i];
					s.tstamp = std::chrono::steady_clock::now();
					s.work = w;
					s.midx = m_index;
				
					_s = s;

					double d = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - search_start).count();

					cout << EthWhite << "Job: " << w.header.abridged() << " Sol: 0x"
						<< dev::toHex(nonce) << EthLime " found in " << dev::getFormattedElapsed(d) << EthReset;
				}
			}
		}

		// Update the hash rate
		//dev::eth::updateHashRate(m_batch_size, m_settings.streams);

	}

#ifdef DEV_BUILD
	// Optionally log job switch time
	if (!shouldStop() && (g_logOptions & LOG_SWITCH))
		cudalog << "Switch time: "
		<< std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - m_workSwitchStart)
		.count()
		<< " ms.";
#endif
}


void Client::workLoop()
{
	WorkPackage w = m_current;
	w.header = dev::h256();
	uint64_t old_period_seed = -1;
	int old_epoch = -1;

	m_search_buf.resize(2);
	m_streams.resize(2);

	try
	{
		if (!w)
		{
			boost::system_time const timeout =
				boost::get_system_time() + boost::posix_time::seconds(3);
			boost::mutex::scoped_lock l(x_work);
			m_new_work_signal.timed_wait(l, timeout);
			goto con;
		}

		if (old_epoch != w.epoch)
		{
			if (!initEpoch())
				goto con;  // This will simply exit the thread
			old_epoch = w.epoch;
			goto con;
		}
		uint64_t period_seed = w.block / 3;
		//if (m_nextProgpowPeriod == 0)
		//{
		//	m_nextProgpowPeriod = period_seed;
		//	m_compileThread = new boost::thread(boost::bind(&asyncCompile, this));
		//}
		//if (old_period_seed != period_seed)
		//{
		//	m_compileThread->join();
		//	// sanity check the next kernel
		//	if (period_seed != m_nextProgpowPeriod)
		//	{
		//		// This shouldn't happen!!! Try to recover
		//		m_nextProgpowPeriod = period_seed;
		//		m_compileThread =
		//			new boost::thread(boost::bind(&asyncCompile, this));
		//		m_compileThread->join();
		//	}
		//	old_period_seed = period_seed;
		//	m_kernelExecIx ^= 1;
		//	cout << "Launching period " << period_seed << " ProgPow kernel";
		//	m_nextProgpowPeriod = period_seed + 1;
		//	m_compileThread = new boost::thread(boost::bind(&dev::eth::CUDAmedi::asyncCompile, this));
		//}

		con:
		// Persist most recent job.
		// Job's differences should be handled at higher level
		uint64_t upper64OfBoundary = (uint64_t)(dev::u64)((dev::u256)w.get_boundary() >> 192);

		// Eventually start searching
		search(w.header.data(), upper64OfBoundary, w.startNonce, w);
		

		// Reset miner and stop working
		CUDA_SAFE_CALL(cudaDeviceReset());
	}
	catch (cuda_runtime_error const& _e)
	{
		string _what = "GPU error: ";
		_what.append(_e.what());
		throw std::runtime_error(_what);
	}
}

string Client::startJson()
{
	json j;

	j["id"] = 1;
	j["jsonrpc"] = "2.0";
	j["method"] = "mining.subscribe";
	j["params"] = { "kawpowminer/1.2.4+commit.0707bca0" };

	return j.dump() + '\n';
}

string Client::authorizeJson()
{
	json j;

	j["id"] = 1;
	j["jsonrpc"] = "2.0";
	j["method"] = "mining.authorize";
	j["params"] = { this->m_wallet + '.' + this->m_rig,"X"};

	return j.dump() + '\n';
}

void Client::proccessResponse(Json::Value& res)
{
	// Store jsonrpc version to test against
	int _rpcVer = res.isMember("jsonrpc") ? 2 : 1;

	bool _isNotification = false;  // Whether or not this message is a reply to previous request or
								   // is a broadcast notification
	bool _isSuccess = false;       // Whether or not this is a succesful or failed response (implies
								   // _isNotification = false)
	string _errReason = "";        // Content of the error reason
	string _method = "";           // The method of the notification (or request from pool)
	unsigned _id = 0;  // This SHOULD be the same id as the request it is responding to (known
					   // exception is ethermine.org using 999)

	// Retrieve essential values
	_id = res.get("id", unsigned(0)).asUInt();
	_isSuccess = res.get("error", Json::Value::null).empty();
	_errReason = (_isSuccess ? "" : processError(res));
	_method = res.get("method", "").asString();
	_isNotification = (_method != "" || _id == unsigned(0));

	Json::Value jReq;
	Json::Value jPrm;

	unsigned prmIdx;

	jPrm = res.get("params", Json::Value::null);
	prmIdx = 1;

	if (jPrm.isArray() && !jPrm.empty())
	{
		m_current.job = jPrm.get(Json::Value::ArrayIndex(0), "").asString();

		
		string sHeaderHash = jPrm.get(Json::Value::ArrayIndex(prmIdx++), "").asString();
		string sSeedHash = jPrm.get(Json::Value::ArrayIndex(prmIdx++), "").asString();
		string sShareTarget = jPrm.get(Json::Value::ArrayIndex(prmIdx++), "").asString();
		bool fCancelJob = jPrm.get(Json::Value::ArrayIndex(prmIdx++), "").asBool();
		uint64_t iBlockHeight = jPrm.get(Json::Value::ArrayIndex(prmIdx++), "").asInt64();
		uint32_t nBlockTargetBits = strtoul(jPrm.get(Json::Value::ArrayIndex(prmIdx++), "").asString().c_str(), nullptr, 16);

		arith_uint256 hashTarget = arith_uint256().SetCompact(nBlockTargetBits);
		std::string sBlockTarget = hashTarget.GetHex();

		{
			try
			{
				/*
				check if the block number is in a valid range
				A year has ~31536000 seconds
				50 years have ~1576800000
				assuming a (very fast) blocktime of 10s:
				==> in 50 years we get 157680000 (=0x9660180) blocks
				*/
				if (iBlockHeight > 0x9660180)
					throw new std::exception();
			}
			catch (const std::exception&)
			{
				m_current.block = -1;
			}
		}

		// coinmine.pl fix
		int l = sShareTarget.length();
		if (l < 66)
			sShareTarget = "0x" + string(66 - l, '0') + sShareTarget.substr(2);

		m_current.seed = dev::h256(sSeedHash);
		m_current.header = dev::h256(sHeaderHash);
		m_current.boundary = dev::h256(sShareTarget);
		m_current.block_boundary = dev::h256(sBlockTarget);
		m_current_timestamp = std::chrono::steady_clock::now();
		m_current.startNonce = m_session->extraNonce;
		m_current.exSizeBytes = m_session->extraNonceSizeBytes;
		m_current.block = iBlockHeight;

		// This will signal to dispatch the job
		// at the end of the transmission.
		m_newjobprocessed = true;
		
	}
}

string Client::sumbitSolution()
{
	Json::Value jReq;

	unsigned id = 40 + _s.midx;
	jReq["id"] = id;
	m_solution_submitted_max_id = max(m_solution_submitted_max_id, id);
	jReq["method"] = "mining.submit";
	jReq["params"] = Json::Value(Json::arrayValue);
	jReq["jsonrpc"] = "2.0";
	jReq["params"].append(m_wallet + "." + m_rig);
	jReq["params"].append(_s.work.job);
	jReq["params"].append(dev::toHex(_s.nonce, dev::HexPrefix::Add));
	jReq["params"].append(_s.work.header.hex(dev::HexPrefix::Add));
	jReq["params"].append(_s.mixHash.hex(dev::HexPrefix::Add));
	if (!m_rig.empty())
		jReq["worker"] = m_rig;

	return jReq.asString() + "\n";
}

std::string padLeft(std::string _value, size_t _length, char _fillChar)
{
	if (_length > _value.size())
		_value.insert(0, (_length - _value.size()), _fillChar);
	return _value;
}


string getResString(char* buff)
{
	string a = "";
	for (size_t i = 0; i < 1024; i++)
	{
		if (buff[i] != '\n')
		{
			a += buff[i];
		}
		else
		{
			break;
		}
	}
	return a;
}


std::string processError(Json::Value& responseObject)
{
	std::string retVar;

	if (responseObject.isMember("error") &&
		!responseObject.get("error", Json::Value::null).isNull())
	{
		if (responseObject["error"].isConvertibleTo(Json::ValueType::stringValue))
		{
			retVar = responseObject.get("error", "Unknown error").asString();
		}
		else if (responseObject["error"].isConvertibleTo(Json::ValueType::arrayValue))
		{
			for (auto i : responseObject["error"])
			{
				retVar += i.asString() + " ";
			}
		}
		else if (responseObject["error"].isConvertibleTo(Json::ValueType::objectValue))
		{
			for (Json::Value::iterator i = responseObject["error"].begin();
				i != responseObject["error"].end(); ++i)
			{
				Json::Value k = i.key();
				Json::Value v = (*i);
				retVar += (std::string)i.name() + ":" + v.asString() + " ";
			}
		}
	}
	else
	{
		retVar = "Unknown error";
	}

	return retVar;
}