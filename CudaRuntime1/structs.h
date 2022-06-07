#pragma once
#include <iostream>
#include "FixedHash.h"
#include <iostream>
#include <boost/format.hpp>
#include <boost/chrono.hpp>
#include <ctime>
#include <chrono>
#include <string>

using namespace std;


struct WorkPackage
{
    WorkPackage() = default;

    explicit operator bool() const { return header != dev::h256(); }

    std::string job;  // Job identifier can be anything. Not necessarily a hash

    dev::h256 boundary;
    dev::h256 header;  ///< When h256() means "pause until notified a new work package is available".
    dev::h256 seed;
    dev::h256 block_boundary;

    dev::h256 get_boundary() const
    {
        if (block_boundary == dev::h256{})
            return boundary;
        else if (boundary < block_boundary)
            return block_boundary;
        else
            return boundary;
    }

    int epoch = -1;
    int block = -1;

    uint64_t startNonce = 0;
    uint16_t exSizeBytes = 0;

    std::string algo = "ethash";
};

struct LogChannel
{
    static const char* name();
};

struct NoteChannel : public LogChannel
{
    static const char* name();
};

struct EpochContext
{
    int epochNumber;
    int lightNumItems;
    size_t lightSize;
    const ethash_hash512* lightCache;
    int dagNumItems;
    uint64_t dagSize;
};

struct ethash_epoch_context
{
    const int epoch_number;
    const int light_cache_num_items;
    const union ethash_hash512* const light_cache;
    const uint32_t* const l1_cache;
    const int full_dataset_num_items;
};

struct Solution
{
    uint64_t nonce;                                // Solution found nonce
    dev::h256 mixHash;                                  // Mix hash
    WorkPackage work;                              // WorkPackage this solution refers to
    std::chrono::steady_clock::time_point tstamp;  // Timestamp of found solution
    unsigned midx;                                 // Originating miner Id
};

struct HwSensorsType
{
    int tempC = 0;
    int fanP = 0;
    double powerW = 0.0;
    string str()
    {
        string _ret = to_string(tempC) + "C " + to_string(fanP) + "%";
        if (powerW)
            _ret.append(boost::str(boost::format("%f") % powerW));
        return _ret;
    };
};

typedef struct ADLTemperature
{
    /// Must be set to the size of the structure
    int iSize;
    /// Temperature in millidegrees Celsius.
    int iTemperature;
} ADLTemperature;

typedef struct ADLFanSpeedValue
{
    /// Must be set to the size of the structure
    int iSize;
    /// Possible valies: \ref ADL_DL_FANCTRL_SPEED_TYPE_PERCENT or \ref
    /// ADL_DL_FANCTRL_SPEED_TYPE_RPM
    int iSpeedType;
    /// Fan speed value
    int iFanSpeed;
    /// The only flag for now is: \ref ADL_DL_FANCTRL_FLAG_USER_DEFINED_SPEED
    int iFlags;
} ADLFanSpeedValue;

/*
 * Handle to hold the function pointers for the entry points we need,
 * and the shared library itself.
 */
typedef struct AdapterInfo
{
    /// \ALL_STRUCT_MEM

    /// Size of the structure.
    int iSize;
    /// The ADL index handle. One GPU may be associated with one or two index handles
    int iAdapterIndex;
    /// The unique device ID associated with this adapter.
    char strUDID[256];
    /// The BUS number associated with this adapter.
    int iBusNumber;
    /// The driver number associated with this adapter.
    int iDeviceNumber;
    /// The function number.
    int iFunctionNumber;
    /// The vendor ID associated with this adapter.
    int iVendorID;
    /// Adapter name.
    char strAdapterName[256];
    /// Display name. For example, "\\Display0" for Windows or ":0:0" for Linux.
    char strDisplayName[256];
    /// Present or not; 1 if present and 0 if not present.It the logical adapter is present, the
    /// display name such as \\.\Display1 can be found from OS
    int iPresent;
    // @}

#if defined(_WIN32)
    /// \WIN_STRUCT_MEM

    /// Exist or not; 1 is exist and 0 is not present.
    int iExist;
    /// Driver registry path.
    char strDriverPath[256];
    /// Driver registry path Ext for.
    char strDriverPathExt[256];
    /// PNP string from Windows.
    char strPNPString[256];
    /// It is generated from EnumDisplayDevices.
    int iOSDisplayIndex;
    // @}
#endif /* (_WIN32) */
} AdapterInfo, * LPAdapterInfo;

typedef void* ADL_CONTEXT_HANDLE;
typedef void* (*ADL_MAIN_MALLOC_CALLBACK) (int);
typedef enum wrap_adlReturn_enum { WRAPADL_OK = 0 } wrap_adlReturn_t;
typedef struct
{
    void* adl_dll;
    int adl_gpucount;
    int log_gpucount;
    int* phys_logi_device_id;
    LPAdapterInfo devs;
    ADL_CONTEXT_HANDLE context;
    wrap_adlReturn_t(*adlMainControlCreate)(ADL_MAIN_MALLOC_CALLBACK, int);
    wrap_adlReturn_t(*adlAdapterNumberOfAdapters)(int*);
    wrap_adlReturn_t(*adlAdapterAdapterInfoGet)(LPAdapterInfo, int);
    wrap_adlReturn_t(*adlAdapterAdapterIdGet)(int, int*);
    wrap_adlReturn_t(*adlOverdrive5TemperatureGet)(int, int, ADLTemperature*);
    wrap_adlReturn_t(*adlOverdrive5FanSpeedGet)(int, int, ADLFanSpeedValue*);
    wrap_adlReturn_t(*adlMainControlRefresh)(void);
    wrap_adlReturn_t(*adlMainControlDestroy)(void);
    wrap_adlReturn_t(*adl2MainControlCreate)(ADL_MAIN_MALLOC_CALLBACK, int, ADL_CONTEXT_HANDLE*);
    wrap_adlReturn_t(*adl2MainControlDestroy)(ADL_CONTEXT_HANDLE);
    wrap_adlReturn_t(*adl2Overdrive6CurrentPowerGet)(ADL_CONTEXT_HANDLE, int, int, int*);
    wrap_adlReturn_t(*adl2MainControlRefresh)(ADL_CONTEXT_HANDLE);
} wrap_adl_handle;

typedef enum wrap_nvmlReturn_enum { WRAPNVML_SUCCESS = 0 } wrap_nvmlReturn_t;
//typedef void* wrap_nvmlDevice_t;
typedef struct
{
    char bus_id_str[16]; /* string form of bus info */
    unsigned int domain;
    unsigned int bus;
    unsigned int device;
    unsigned int pci_device_id; /* combined device and vendor id */
    unsigned int pci_subsystem_id;
    unsigned int res0; /* NVML internal use only */
    unsigned int res1;
    unsigned int res2;
    unsigned int res3;
} wrap_nvmlPciInfo_t;

//typedef struct
//{
//    void* nvml_dll;
//    int nvml_gpucount;
//    unsigned int* nvml_pci_domain_id;
//    unsigned int* nvml_pci_bus_id;
//    unsigned int* nvml_pci_device_id;
//    wrap_nvmlDevice_t* devs;
//    wrap_nvmlReturn_t(*nvmlInit)(void);
//    wrap_nvmlReturn_t(*nvmlDeviceGetCount)(int*);
//    wrap_nvmlReturn_t(*nvmlDeviceGetHandleByIndex)(int, wrap_nvmlDevice_t*);
//    wrap_nvmlReturn_t(*nvmlDeviceGetPciInfo)(wrap_nvmlDevice_t, wrap_nvmlPciInfo_t*);
//    wrap_nvmlReturn_t(*nvmlDeviceGetName)(wrap_nvmlDevice_t, char*, int);
//    wrap_nvmlReturn_t(*nvmlDeviceGetTemperature)(wrap_nvmlDevice_t, int, unsigned int*);
//    wrap_nvmlReturn_t(*nvmlDeviceGetFanSpeed)(wrap_nvmlDevice_t, unsigned int*);
//    wrap_nvmlReturn_t(*nvmlDeviceGetPowerUsage)(wrap_nvmlDevice_t, unsigned int*);
//    wrap_nvmlReturn_t(*nvmlShutdown)(void);
//} wrap_nvml_handle;

enum MinerPauseEnum
{
    PauseDueToOverHeating,
    PauseDueToAPIRequest,
    PauseDueToFarmPaused,
    PauseDueToInsufficientMemory,
    PauseDueToInitEpochError,
    Pause_MAX  // Must always be last as a placeholder of max count
};

struct SolutionAccountType
{
    unsigned accepted = 0;
    unsigned rejected = 0;
    unsigned wasted = 0;
    unsigned failed = 0;
    std::chrono::steady_clock::time_point tstamp = std::chrono::steady_clock::now();
    string str()
    {
        string _ret = "A" + to_string(accepted);
        if (wasted)
            _ret.append(":W" + to_string(wasted));
        if (rejected)
            _ret.append(":R" + to_string(rejected));
        if (failed)
            _ret.append(":F" + to_string(failed));
        return _ret;
    };
};


enum class DeviceSubscriptionTypeEnum
{
    None,
    OpenCL,
    Cuda,
    Cpu

};
enum class DeviceTypeEnum
{
    Unknown,
    Cpu,
    Gpu,
    Accelerator
};

enum class ClPlatformTypeEnum
{
    Unknown,
    Amd,
    Clover,
    Nvidia
};

enum class MinerType
{
    Mixed,
    CL,
    CUDA,
    CPU
};

enum class HwMonitorInfoType
{
    UNKNOWN,
    NVIDIA,
    AMD,
    CPU
};

//enum class ClPlatformTypeEnum
//{
//    Unknown,
//    Amd,
//    Clover,
//    Nvidia
//};

enum class SolutionAccountingEnum
{
    Accepted,
    Rejected,
    Wasted,
    Failed
};

struct MinerSettings
{
    vector<unsigned> devices;
};

// Holds settings for CUDA Miner
struct CUSettings : public MinerSettings
{
    unsigned streams = 2;
    unsigned schedule = 4;
    unsigned gridSize = 256;
    unsigned blockSize = 512;
    unsigned parallelHash = 4;
};

// Holds settings for OpenCL Miner
struct CLSettings : public MinerSettings
{
    bool noBinary = false;
    unsigned globalWorkSize = 0;
    unsigned globalWorkSizeMultiplier = 32768;
    unsigned localWorkSize = 256;
};

// Holds settings for CPU Miner
struct CPSettings : public MinerSettings
{
};
struct DeviceDescriptor
{
    DeviceTypeEnum type = DeviceTypeEnum::Unknown;
    DeviceSubscriptionTypeEnum subscriptionType = DeviceSubscriptionTypeEnum::None;

    string uniqueId;     // For GPUs this is the PCI ID
    size_t totalMemory;  // Total memory available on device
    size_t freeMemory;   // Free memory available on device
    string name;         // Device Name

    bool clDetected;  // For OpenCL detected devices
    string clName;
    unsigned int clPlatformId;
    string clPlatformName;
    ClPlatformTypeEnum clPlatformType = ClPlatformTypeEnum::Unknown;
    string clPlatformVersion;
    unsigned int clPlatformVersionMajor;
    unsigned int clPlatformVersionMinor;
    unsigned int clDeviceOrdinal;
    unsigned int clDeviceIndex;
    string clDeviceVersion;
    unsigned int clDeviceVersionMajor;
    unsigned int clDeviceVersionMinor;
    string clBoardName;
    size_t clMaxMemAlloc;
    size_t clMaxWorkGroup;
    unsigned int clMaxComputeUnits;
    string clNvCompute;
    unsigned int clNvComputeMajor;
    unsigned int clNvComputeMinor;

    bool cuDetected;  // For CUDA detected devices
    string cuName;
    unsigned int cuDeviceOrdinal;
    unsigned int cuDeviceIndex;
    string cuCompute;
    unsigned int cuComputeMajor;
    unsigned int cuComputeMinor;

    int cpCpuNumer;   // For CPU
};

struct ThreadLocalLogName
{
    ThreadLocalLogName(char const* _name) { name = _name; }
    thread_local static char const* name;
};

//enum class SolutionAccountingEnum
//{
//    Accepted,
//    Rejected,
//    Wasted,
//    Failed
//};

struct Result
{
    dev::h256 value;
    dev::h256 mixHash;
};

#define MAX_SEARCH_RESULTS 4U

typedef struct {
    uint32_t count;
    struct {
        // One word for gid and 8 for mix hash
        uint32_t gid;
        uint32_t mix[8];
    } result[MAX_SEARCH_RESULTS];
} Search_results;



struct cuda_runtime_error : public virtual std::runtime_error
{
    cuda_runtime_error(std::string msg) : std::runtime_error(msg) {}
};

#define CUDA_SAFE_CALL(call)				\
do {							\
	cudaError_t result = call;				\
	if (cudaSuccess != result) {			\
		std::stringstream ss;			\
		ss << "CUDA error in func " 		\
            << __FUNCTION__ 		\
			<< " at line "			\
			<< __LINE__			\
			<< " calling " #call " failed with error "     \
			<< cudaGetErrorString(result);	\
		throw cuda_runtime_error(ss.str());	\
	}						\
} while (0)

#define CU_SAFE_CALL(call)								\
do {													\
	CUresult result = call;								\
	if (result != CUDA_SUCCESS) {						\
		std::stringstream ss;							\
		const char *msg;								\
		cuGetErrorName(result, &msg);                   \
		ss << "CUDA error in func " 					\
			<< __FUNCTION__ 							\
			<< " at line "								\
			<< __LINE__									\
			<< " calling " #call " failed with error "  \
			<< msg;										\
		throw cuda_runtime_error(ss.str());				\
	}													\
} while (0)

#define NVRTC_SAFE_CALL(call)                                                                     \
    do                                                                                            \
    {                                                                                             \
        nvrtcResult result = call;                                                                \
        if (result != NVRTC_SUCCESS)                                                              \
        {                                                                                         \
            std::stringstream ss;                                                                 \
            ss << "CUDA NVRTC error in func " << __FUNCTION__ << " at line " << __LINE__          \
               << " calling " #call " failed with error " << nvrtcGetErrorString(result) << '\n'; \
            throw cuda_runtime_error(ss.str());                                                   \
        }                                                                                         \
    } while (0)


#define EthReset "\x1b[0m"  // Text Reset

// Regular Colors
#define EthBlack "\x1b[30m"   // Black
#define EthCoal "\x1b[90m"    // Black
#define EthGray "\x1b[37m"    // White
#define EthWhite "\x1b[97m"   // White
#define EthMaroon "\x1b[31m"  // Red
#define EthRed "\x1b[91m"     // Red
#define EthGreen "\x1b[32m"   // Green
#define EthLime "\x1b[92m"    // Green
#define EthOrange "\x1b[33m"  // Yellow
#define EthYellow "\x1b[93m"  // Yellow
#define EthNavy "\x1b[34m"    // Blue
#define EthBlue "\x1b[94m"    // Blue
#define EthViolet "\x1b[35m"  // Purple
#define EthPurple "\x1b[95m"  // Purple
#define EthTeal "\x1b[36m"    // Cyan
#define EthCyan "\x1b[96m"    // Cyan

#define EthBlackBold "\x1b[1;30m"   // Black
#define EthCoalBold "\x1b[1;90m"    // Black
#define EthGrayBold "\x1b[1;37m"    // White
#define EthWhiteBold "\x1b[1;97m"   // White
#define EthMaroonBold "\x1b[1;31m"  // Red
#define EthRedBold "\x1b[1;91m"     // Red
#define EthGreenBold "\x1b[1;32m"   // Green
#define EthLimeBold "\x1b[1;92m"    // Green
#define EthOrangeBold "\x1b[1;33m"  // Yellow
#define EthYellowBold "\x1b[1;93m"  // Yellow
#define EthNavyBold "\x1b[1;34m"    // Blue
#define EthBlueBold "\x1b[1;94m"    // Blue
#define EthVioletBold "\x1b[1;35m"  // Purple
#define EthPurpleBold "\x1b[1;95m"  // Purple
#define EthTealBold "\x1b[1;36m"    // Cyan
#define EthCyanBold "\x1b[1;96m"    // Cyan

// Background
#define EthOnBlack "\x1b[40m"    // Black
#define EthOnCoal "\x1b[100m"    // Black
#define EthOnGray "\x1b[47m"     // White
#define EthOnWhite "\x1b[107m"   // White
#define EthOnMaroon "\x1b[41m"   // Red
#define EthOnRed "\x1b[101m"     // Red
#define EthOnGreen "\x1b[42m"    // Green
#define EthOnLime "\x1b[102m"    // Green
#define EthOnOrange "\x1b[43m"   // Yellow
#define EthOnYellow "\x1b[103m"  // Yellow
#define EthOnNavy "\x1b[44m"     // Blue
#define EthOnBlue "\x1b[104m"    // Blue
#define EthOnViolet "\x1b[45m"   // Purple
#define EthOnPurple "\x1b[105m"  // Purple
#define EthOnTeal "\x1b[46m"     // Cyan
#define EthOnCyan "\x1b[106m"    // Cyan

// Underline
#define EthBlackUnder "\x1b[4;30m"   // Black
#define EthGrayUnder "\x1b[4;37m"    // White
#define EthMaroonUnder "\x1b[4;31m"  // Red
#define EthGreenUnder "\x1b[4;32m"   // Green
#define EthOrangeUnder "\x1b[4;33m"  // Yellow
#define EthNavyUnder "\x1b[4;34m"    // Blue
#define EthVioletUnder "\x1b[4;35m"  // Purple
#define EthTealUnder "\x1b[4;36m"    // Cyan
