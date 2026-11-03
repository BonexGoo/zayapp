#include <boss.hpp>
#include "hidapi.hpp"

#include <platform/boss_platform.hpp>
#if BOSS_WINDOWS
    #include <windows.h>
#endif

struct hid_api_version
{
    int major;
    int minor;
    int patch;
};

typedef enum
{
    HID_API_BUS_UNKNOWN = 0x00, // Unknown bus type
    HID_API_BUS_USB = 0x01, // USB bus
    HID_API_BUS_BLUETOOTH = 0x02, // Bluetooth or Bluetooth LE bus
    HID_API_BUS_I2C = 0x03, // I2C bus
    HID_API_BUS_SPI = 0x04, // SPI bus
} hid_bus_type;

struct hid_device_info
{
    char* path; // Platform-specific device path
    unsigned short vendor_id; // Device Vendor ID
    unsigned short product_id; // Device Product ID
    wchar_t* serial_number; // Serial Number
    unsigned short release_number; // Device Release Number in binary-coded decimal, also known as Device Version Number
    wchar_t* manufacturer_string; // Manufacturer String
    wchar_t* product_string; // Product string
    unsigned short usage_page; // Usage Page for this Device/Interface (Windows/Mac/hidraw only)
    unsigned short usage; // Usage for this Device/Interface (Windows/Mac/hidraw only)
    int interface_number; // The USB interface which this logical device represents.
    struct hid_device_info* next; // Pointer to the next device
    hid_bus_type bus_type; // Underlying bus type
};

struct hid_device_;
typedef struct hid_device_ hid_device;

typedef int                           (*Method_hid_init)(void);
typedef int                           (*Method_hid_exit)(void);
typedef struct hid_device_info*       (*Method_hid_enumerate)(unsigned short vendor_id, unsigned short product_id);
typedef void                          (*Method_hid_free_enumeration)(struct hid_device_info* devs);
typedef hid_device*                   (*Method_hid_open)(unsigned short vendor_id, unsigned short product_id, const wchar_t* serial_number);
typedef hid_device*                   (*Method_hid_open_path)(const char* path);
typedef int                           (*Method_hid_write)(hid_device* dev, const unsigned char* data, size_t length);
typedef int                           (*Method_hid_read_timeout)(hid_device* dev, unsigned char* data, size_t length, int milliseconds);
typedef int                           (*Method_hid_read)(hid_device* dev, unsigned char* data, size_t length);
typedef int                           (*Method_hid_set_nonblocking)(hid_device* dev, int nonblock);
typedef int                           (*Method_hid_send_feature_report)(hid_device* dev, const unsigned char* data, size_t length);
typedef int                           (*Method_hid_get_feature_report)(hid_device* dev, unsigned char* data, size_t length);
typedef int                           (*Method_hid_get_input_report)(hid_device* dev, unsigned char* data, size_t length);
typedef void                          (*Method_hid_close)(hid_device* dev);
typedef int                           (*Method_hid_get_manufacturer_string)(hid_device* dev, wchar_t* string, size_t maxlen);
typedef int                           (*Method_hid_get_product_string)(hid_device* dev, wchar_t* string, size_t maxlen);
typedef int                           (*Method_hid_get_serial_number_string)(hid_device* dev, wchar_t* string, size_t maxlen);
typedef struct hid_device_info*       (*Method_hid_get_device_info)(hid_device* dev);
typedef int                           (*Method_hid_get_indexed_string)(hid_device* dev, int string_index, wchar_t* string, size_t maxlen);
typedef const wchar_t*                (*Method_hid_error)(hid_device* dev);
typedef const struct hid_api_version* (*Method_hid_version)(void);
typedef const char*                   (*Method_hid_version_str)(void);

class HIDCore : public HIDApi
{
public:
    HIDCore()
    {
        #if BOSS_WINDOWS
            #if BOSS_X64
                mLibrary = LoadLibraryA(Platform::File::RootForAssets() + "hidapi-win/x64/hidapi.dll");
            #else
                mLibrary = LoadLibraryA(Platform::File::RootForAssets() + "hidapi-win/x86/hidapi.dll");
            #endif
        #endif

        #if BOSS_WINDOWS
            mMethod_hid_init                     = (Method_hid_init) GetProcAddress(mLibrary, "hid_init");
            mMethod_hid_exit                     = (Method_hid_exit) GetProcAddress(mLibrary, "hid_exit");
            mMethod_hid_enumerate                = (Method_hid_enumerate) GetProcAddress(mLibrary, "hid_enumerate");
            mMethod_hid_free_enumeration         = (Method_hid_free_enumeration) GetProcAddress(mLibrary, "hid_free_enumeration");
            mMethod_hid_open                     = (Method_hid_open) GetProcAddress(mLibrary, "hid_open");
            mMethod_hid_open_path                = (Method_hid_open_path) GetProcAddress(mLibrary, "hid_open_path");
            mMethod_hid_write                    = (Method_hid_write) GetProcAddress(mLibrary, "hid_write");
            mMethod_hid_read_timeout             = (Method_hid_read_timeout) GetProcAddress(mLibrary, "hid_read_timeout");
            mMethod_hid_read                     = (Method_hid_read) GetProcAddress(mLibrary, "hid_read");
            mMethod_hid_set_nonblocking          = (Method_hid_set_nonblocking) GetProcAddress(mLibrary, "hid_set_nonblocking");
            mMethod_hid_send_feature_report      = (Method_hid_send_feature_report) GetProcAddress(mLibrary, "hid_send_feature_report");
            mMethod_hid_get_feature_report       = (Method_hid_get_feature_report) GetProcAddress(mLibrary, "hid_get_feature_report");
            mMethod_hid_get_input_report         = (Method_hid_get_input_report) GetProcAddress(mLibrary, "hid_get_input_report");
            mMethod_hid_close                    = (Method_hid_close) GetProcAddress(mLibrary, "hid_close");
            mMethod_hid_get_manufacturer_string  = (Method_hid_get_manufacturer_string) GetProcAddress(mLibrary, "hid_get_manufacturer_string");
            mMethod_hid_get_product_string       = (Method_hid_get_product_string) GetProcAddress(mLibrary, "hid_get_product_string");
            mMethod_hid_get_serial_number_string = (Method_hid_get_serial_number_string) GetProcAddress(mLibrary, "hid_get_serial_number_string");
            mMethod_hid_get_device_info          = (Method_hid_get_device_info) GetProcAddress(mLibrary, "hid_get_device_info");
            mMethod_hid_get_indexed_string       = (Method_hid_get_indexed_string) GetProcAddress(mLibrary, "hid_get_indexed_string");
            mMethod_hid_error                    = (Method_hid_error) GetProcAddress(mLibrary, "hid_error");
            mMethod_hid_version                  = (Method_hid_version) GetProcAddress(mLibrary, "hid_version");
            mMethod_hid_version_str              = (Method_hid_version_str) GetProcAddress(mLibrary, "hid_version_str");
        #endif
    }
    ~HIDCore()
    {
        #if BOSS_WINDOWS
            FreeLibrary(mLibrary);
        #endif
    }

public:
    Context GetDevices() override
    {
        Context Collector;
        auto HIDInfo = mMethod_hid_enumerate(0, 0);
        for(struct hid_device_info* iInfo = HIDInfo; iInfo != nullptr; iInfo = iInfo->next)
        {
            auto& NewHID = Collector.AtAdding();
            NewHID.At("path").Set(iInfo->path);
            NewHID.At("vendor_id").Set(String::FromInteger(iInfo->vendor_id));
            NewHID.At("product_id").Set(String::FromInteger(iInfo->product_id));
            NewHID.At("serial_number").Set(String::FromWChars(iInfo->serial_number));
            NewHID.At("release_number").Set(String::FromInteger(iInfo->release_number));
            NewHID.At("manufacturer_string").Set(String::FromWChars(iInfo->manufacturer_string));
            NewHID.At("product_string").Set(String::FromWChars(iInfo->product_string));
            NewHID.At("usage_page").Set(String::FromInteger(iInfo->usage_page));
            NewHID.At("usage").Set(String::FromInteger(iInfo->usage));
            NewHID.At("interface_number").Set(String::FromInteger(iInfo->interface_number));
            switch(iInfo->bus_type)
            {
            case HID_API_BUS_USB: NewHID.At("bus_type").Set("USB"); break;
            case HID_API_BUS_BLUETOOTH: NewHID.At("bus_type").Set("BLUETOOTH"); break;
            case HID_API_BUS_I2C: NewHID.At("bus_type").Set("I2C"); break;
            case HID_API_BUS_SPI: NewHID.At("bus_type").Set("SPI"); break;
            default: NewHID.At("bus_type").Set("UNKNOWN"); break;
            }
        }
        mMethod_hid_free_enumeration(HIDInfo);
        return Collector;
    }
    bool OpenForID(uint32 vendorid, uint32 productid) override
    {
        Close();
        mDevice = mMethod_hid_open(vendorid, productid, nullptr);
        return (mDevice != nullptr);
    }
    bool OpenForPath(chars path) override
    {
        Close();
        mDevice = mMethod_hid_open_path(path);
        return (mDevice != nullptr);
    }
    void Close() override
    {
        if(mDevice)
        {
            mMethod_hid_close(mDevice);
            mDevice = nullptr;
        }
    }
    sint32 Read(uint08* out, sint32 maxsize, sint32 timeout) override
    {
        if(mDevice)
            return mMethod_hid_read_timeout(mDevice, out, maxsize, timeout);
        return 0;
    }
    sint32 Write(bytes data, sint32 size) override
    {
        if(mDevice)
            return mMethod_hid_write(mDevice, data, size);
        return -1;
    }

public:
    inline bool HasLibrary() const
    {
        return (mLibrary != nullptr);
    }
    bool Init()
    {
        return (mMethod_hid_init() == 0);
    }
    void Exit()
    {
        Close();
        mMethod_hid_exit();
    }

private:
    #if BOSS_WINDOWS
    HINSTANCE mLibrary {nullptr};
    #else
        void* mLibrary {nullptr};
    #endif
    Method_hid_init                     mMethod_hid_init {nullptr};
    Method_hid_exit                     mMethod_hid_exit {nullptr};
    Method_hid_enumerate                mMethod_hid_enumerate {nullptr};
    Method_hid_free_enumeration         mMethod_hid_free_enumeration {nullptr};
    Method_hid_open                     mMethod_hid_open {nullptr};
    Method_hid_open_path                mMethod_hid_open_path {nullptr};
    Method_hid_write                    mMethod_hid_write {nullptr};
    Method_hid_read_timeout             mMethod_hid_read_timeout {nullptr};
    Method_hid_read                     mMethod_hid_read {nullptr};
    Method_hid_set_nonblocking          mMethod_hid_set_nonblocking {nullptr};
    Method_hid_send_feature_report      mMethod_hid_send_feature_report {nullptr};
    Method_hid_get_feature_report       mMethod_hid_get_feature_report {nullptr};
    Method_hid_get_input_report         mMethod_hid_get_input_report {nullptr};
    Method_hid_close                    mMethod_hid_close {nullptr};
    Method_hid_get_manufacturer_string  mMethod_hid_get_manufacturer_string {nullptr};
    Method_hid_get_product_string       mMethod_hid_get_product_string {nullptr};
    Method_hid_get_serial_number_string mMethod_hid_get_serial_number_string {nullptr};
    Method_hid_get_device_info          mMethod_hid_get_device_info {nullptr};
    Method_hid_get_indexed_string       mMethod_hid_get_indexed_string {nullptr};
    Method_hid_error                    mMethod_hid_error {nullptr};
    Method_hid_version                  mMethod_hid_version {nullptr};
    Method_hid_version_str              mMethod_hid_version_str {nullptr};

private:
    hid_device* mDevice {nullptr};
};

HIDApi* HIDApi::Create()
{
    HIDCore* NewHID = new HIDCore();
    if(NewHID->HasLibrary() && NewHID->Init())
        return NewHID;
    delete NewHID;
    return nullptr;
}

void HIDApi::Release(HIDApi*& api)
{
    if(api)
    {
        ((HIDCore*) api)->Exit();
        delete (HIDCore*) api;
        api = nullptr;
    }
}
