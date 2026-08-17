#pragma once

#ifdef _WIN32
#define INTEROP_EXPORT __declspec(dllexport)
#pragma warning(disable : 4244) 
#else
#define INTEROP_EXPORT __attribute__((visibility("default")))
#endif

#include <PixelFormats.h>
#include "callbacks.h"
#include <string>
#include <memory>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <regex>
#include <map>
#include <stdexcept>
#include <filesystem>
#include <cstdint>
#ifdef _WIN32
#include <Winsock2.h>
#include <Windows.h>
#include <Unknwn.h>
#include <shlobj_core.h>
#include <Knownfolders.h>
#else
#include <dlfcn.h>
#include <pwd.h>
#include <unistd.h>
struct IUnknown;
#endif

#ifdef _WIN32
    const static wchar_t BridgeVersion[] = L"2.4.11";
#else
    const static char BridgeVersion[] = "2.4.11";
#endif

const uint32_t FIRST_LOOKING_GLASS_DEVICE = -1;

typedef unsigned long WINDOW_HANDLE;

struct CalibrationSubpixelCell
{
	float ROffsetX;
	float ROffsetY;
	float GOffsetX;
	float GOffsetY;
	float BOffsetX;
	float BOffsetY;
};

#ifdef _WIN32
extern "C" INTEROP_EXPORT bool initialize_bridge(const wchar_t *app_name);
#else
extern "C" INTEROP_EXPORT bool initialize_bridge(const char *app_name);
#endif

extern "C" INTEROP_EXPORT bool uninitialize_bridge();

// mlc: call twice -- first set postfix = nullptr, it will return the other params.
extern "C" INTEROP_EXPORT bool get_bridge_version(unsigned long* major, unsigned long *minor, unsigned long* build, int* number_of_postfix_wchars, wchar_t* postfix);

extern "C" INTEROP_EXPORT bool instance_window_gl(WINDOW_HANDLE *wnd, unsigned long head_index = FIRST_LOOKING_GLASS_DEVICE);

extern "C" INTEROP_EXPORT bool instance_offscreen_window(WINDOW_HANDLE *wnd, unsigned long width, unsigned long height, const wchar_t* calibration_path);

extern "C" INTEROP_EXPORT bool quiltify_rgbd(WINDOW_HANDLE wnd, unsigned long columns, unsigned long rows, unsigned long views, float aspect, float zoom, float cam_dist, float fov, float crop_pos_x, float crop_pos_y, unsigned long depth_inversion, unsigned long chroma_depth, unsigned long depth_loc, float depthiness, float depth_cutoff, float focus, const wchar_t* input_path, const wchar_t* output_path);

extern "C" INTEROP_EXPORT bool get_window_dimensions(WINDOW_HANDLE wnd, unsigned long* width, unsigned long* height);
extern "C" INTEROP_EXPORT bool get_window_position(WINDOW_HANDLE wnd, long* x, long* y);

extern "C" INTEROP_EXPORT bool get_max_texture_size(WINDOW_HANDLE wnd, unsigned long* size);

extern "C" INTEROP_EXPORT bool set_interop_quilt_texture_gl(WINDOW_HANDLE wnd, unsigned long long texture, PixelFormats format, unsigned long width, unsigned long height, unsigned long vx, unsigned long vy, float aspect, float zoom);

extern "C" INTEROP_EXPORT bool draw_interop_quilt_texture_gl(WINDOW_HANDLE wnd, unsigned long long texture, PixelFormats format, unsigned long width, unsigned long height, unsigned long vx, unsigned long vy, float aspect, float zoom);

extern "C" INTEROP_EXPORT bool show_window(WINDOW_HANDLE wnd, bool flag);

#ifdef WIN32
extern "C" INTEROP_EXPORT bool save_texture_to_file_gl(WINDOW_HANDLE wnd, wchar_t* filename, unsigned long long texture, PixelFormats format, unsigned long width, unsigned long height);
#else
extern "C" INTEROP_EXPORT bool save_texture_to_file_gl(WINDOW_HANDLE wnd, char* filename, unsigned long long texture, PixelFormats format, unsigned long width, unsigned long height);
#endif

#ifdef WIN32
extern "C" INTEROP_EXPORT bool save_image_to_file(WINDOW_HANDLE wnd, wchar_t* filename, void* image, PixelFormats format, unsigned long width, unsigned long height);
#else
extern "C" INTEROP_EXPORT bool save_image_to_file(WINDOW_HANDLE wnd, char* filename, void* image, PixelFormats format, unsigned long width, unsigned long height);
#endif

#ifdef WIN32
extern "C" INTEROP_EXPORT bool device_from_resource_dx(IUnknown * dx_resource, IUnknown **dx_device);
extern "C" INTEROP_EXPORT bool release_device_dx(IUnknown * dx_device);
extern "C" INTEROP_EXPORT bool instance_window_dx(IUnknown *dx_device, WINDOW_HANDLE *wnd, unsigned long head_index = FIRST_LOOKING_GLASS_DEVICE);
extern "C" INTEROP_EXPORT bool register_texture_dx(WINDOW_HANDLE wnd, IUnknown *dx_texture);
extern "C" INTEROP_EXPORT bool unregister_texture_dx(WINDOW_HANDLE wnd, IUnknown *dx_texture);
extern "C" INTEROP_EXPORT bool save_texture_to_file_dx(WINDOW_HANDLE wnd, wchar_t* filename, IUnknown * dx_texture);
extern "C" INTEROP_EXPORT bool draw_interop_quilt_texture_dx(WINDOW_HANDLE wnd, IUnknown * dx_texture, unsigned long vx, unsigned long vy, float aspect, float zoom);
extern "C" INTEROP_EXPORT bool create_texture_dx(WINDOW_HANDLE wnd, unsigned long width, unsigned long height, IUnknown **dx_texture);
extern "C" INTEROP_EXPORT bool release_texture_dx(WINDOW_HANDLE wnd, IUnknown *dx_texture);
extern "C" INTEROP_EXPORT bool copy_texture_dx(WINDOW_HANDLE wnd, IUnknown *src, IUnknown *dest);
#endif

#ifdef __APPLE__
extern "C" INTEROP_EXPORT bool instance_window_metal(void *metal_device, WINDOW_HANDLE *wnd, unsigned long head_index = FIRST_LOOKING_GLASS_DEVICE);
extern "C" INTEROP_EXPORT bool create_metal_texture_with_iosurface(WINDOW_HANDLE wnd, void* descriptor, void** texture);
extern "C" INTEROP_EXPORT bool copy_metal_texture(WINDOW_HANDLE wnd, void* src, void* dest);
extern "C" INTEROP_EXPORT bool release_metal_texture(WINDOW_HANDLE wnd, void* texture);
extern "C" INTEROP_EXPORT bool save_metal_texture_to_file(WINDOW_HANDLE wnd, char* filename, void* texture, PixelFormats format, unsigned long width, unsigned long height);
extern "C" INTEROP_EXPORT bool draw_interop_quilt_texture_metal(WINDOW_HANDLE wnd, void* texture, unsigned long vx, unsigned long vy, float aspect, float zoom);
#endif

// mlc: call twice -- first set cells = nullptr, it will return the other params.
// Then alloc the SubpixelCell array on the client side with the apropos size and call again with that pointer.
extern "C" INTEROP_EXPORT bool get_calibration(WINDOW_HANDLE wnd, 
	                                           float* center, 
	                                           float *pitch, 
	                                           float *slope, 
	                                           int* width, 
	                                           int* height, 
	                                           float* dpi, 
	                                           float* flip_x,
                                               int *invView,
									           float *viewcone,
									           float *fringe,
                                               int* cell_pattern_mode,
											   int* number_of_cells,
	                                           CalibrationSubpixelCell* cells);

// mlc: call twice -- first set device_name = nullptr, it will return the other params.
// Then alloc the device_name array on the client side with the apropos size and call again with that pointer.
extern "C" INTEROP_EXPORT bool get_device_name(WINDOW_HANDLE wnd, int* number_of_device_name_wchars, wchar_t* device_name);

// mlc: call twice -- first set serial = nullptr, it will return the other params.
// Then alloc the serial array on the client side with the apropos size and call again with that pointer.
extern "C" INTEROP_EXPORT bool get_device_serial(WINDOW_HANDLE wnd, int* number_of_serial_wchars, wchar_t* serial);

extern "C" INTEROP_EXPORT bool get_default_quilt_settings(WINDOW_HANDLE wnd, float* aspect, int* quilt_width, int* quilt_height, int* quilt_columns, int* quilt_rows);

extern "C" INTEROP_EXPORT bool get_display_for_window(WINDOW_HANDLE wnd, unsigned long* display_index);

extern "C" INTEROP_EXPORT bool get_device_type(WINDOW_HANDLE wnd, int* hw_enum);

extern "C" INTEROP_EXPORT bool get_viewcone(WINDOW_HANDLE wnd, float* viewcone);

// mlc: call twice -- first set indices = nullptr, it will return the other params.
// Then alloc the indices array on the client side with the apropos size and call again with that pointer.
extern "C" INTEROP_EXPORT bool get_displays(int* number_of_indices, unsigned long* indices);

// mlc: call twice -- first set device_name = nullptr, it will return the other params.
// Then alloc the device_name array on the client side with the apropos size and call again with that pointer.
extern "C" INTEROP_EXPORT bool get_device_name_for_display(unsigned long display_index, int* number_of_device_name_wchars, wchar_t* device_name);

// mlc: call twice -- first set serial = nullptr, it will return the other params.
// Then alloc the serial array on the client side with the apropos size and call again with that pointer.
extern "C" INTEROP_EXPORT bool get_device_serial_for_display(unsigned long display_index, int* number_of_serial_wchars, wchar_t* serial);

extern "C" INTEROP_EXPORT bool get_dimensions_for_display(unsigned long display_index, unsigned long* width, unsigned long* height);
extern "C" INTEROP_EXPORT bool get_window_position_for_display(unsigned long display_index, long* x, long* y);

extern "C" INTEROP_EXPORT bool get_device_type_for_display(unsigned long display_index, int* hw_enum);

// mlc: call twice -- first set cells = nullptr, it will return the other params.
// Then alloc the SubpixelCell array on the client side with the apropos size and call again with that pointer.
extern "C" INTEROP_EXPORT bool get_calibration_for_display(unsigned long display_index, 
	                                           float* center, 
	                                           float *pitch, 
	                                           float *slope, 
	                                           int* width, 
	                                           int* height, 
	                                           float* dpi, 
	                                           float* flip_x,
                                               int* invView,
									           float* viewcone,
									           float* fringe,
                                               int* cell_pattern_mode,
											   int* number_of_cells,
	                                           CalibrationSubpixelCell* cells);

extern "C" INTEROP_EXPORT bool get_invview_for_display(unsigned long display_index, int* invview);
extern "C" INTEROP_EXPORT bool get_ri_for_display(unsigned long display_index, int* ri);
extern "C" INTEROP_EXPORT bool get_bi_for_display(unsigned long display_index, int *bi);
extern "C" INTEROP_EXPORT bool get_tilt_for_display(unsigned long display_index, float* tilt);
extern "C" INTEROP_EXPORT bool get_displayaspect_for_display(unsigned long display_index, float* displayaspect);
extern "C" INTEROP_EXPORT bool get_fringe_for_display(unsigned long display_index, float* fringe);
extern "C" INTEROP_EXPORT bool get_subp_for_display(unsigned long display_index, float* subp);
extern "C" INTEROP_EXPORT bool get_viewcone_for_display(unsigned long display_index, float* viewcone);
extern "C" INTEROP_EXPORT bool get_pitch_for_display(unsigned long display_index, float* pitch);
extern "C" INTEROP_EXPORT bool get_center_for_display(unsigned long display_index, float* center);
extern "C" INTEROP_EXPORT bool get_default_quilt_settings_for_display(unsigned long display_index, float* aspect, int* quilt_width, int* quilt_height, int* quilt_columns, int* quilt_rows);

extern "C" INTEROP_EXPORT bool get_invview(WINDOW_HANDLE wnd, int* invview);
extern "C" INTEROP_EXPORT bool get_ri(WINDOW_HANDLE wnd, int* ri);
extern "C" INTEROP_EXPORT bool get_bi(WINDOW_HANDLE wnd, int *bi);
extern "C" INTEROP_EXPORT bool get_tilt(WINDOW_HANDLE wnd, float* tilt);
extern "C" INTEROP_EXPORT bool get_displayaspect(WINDOW_HANDLE wnd, float* displayaspect);
extern "C" INTEROP_EXPORT bool get_fringe(WINDOW_HANDLE wnd, float* fringe);
extern "C" INTEROP_EXPORT bool get_subp(WINDOW_HANDLE wnd, float* subp);
extern "C" INTEROP_EXPORT bool get_pitch(WINDOW_HANDLE wnd, float* pitch);
extern "C" INTEROP_EXPORT bool get_center(WINDOW_HANDLE wnd, float* center);

// mlc: the controller class provides a way to get to bridge in "SDK Mode" without copying dependencies.
// When you initialize, if you pass in no version: it will default to looking for whichever version
// of Bridge this header was compiled with.

// You may also pass in a specific version, if that version is installed it will be used.
// Otherwise it will attempt to locate the closest matching version.
// You may also leave the version blank and pass in a full path to the folder where bridge is installed.
class Controller
{
protected:
    class DynamicLibraryLoader
    {
    private:
        std::map<std::string, void*> functionCache;

    public:
#ifdef _WIN32
        template<typename T>
        T LoadFunction(const std::wstring& path, const std::string& functionName)
        {
            auto key = std::string(path.begin(), path.end()) + functionName;
            auto it = functionCache.find(key);
            if (it != functionCache.end())
            {
                return reinterpret_cast<T>(it->second);
            }

            HMODULE handle = LoadLibraryW(path.c_str());
            if (!handle)
            {
                return nullptr;
            }

            FARPROC funcPtr = GetProcAddress(handle, functionName.c_str());
            if (!funcPtr)
            {
                return nullptr;
            }

            functionCache[key] = funcPtr;
            return reinterpret_cast<T>(funcPtr);
        }
#else
        template<typename T>
        T LoadFunction(const std::string& path, const std::string& functionName)
        {
            auto key = path + functionName;
            auto it = functionCache.find(key);
            if (it != functionCache.end())
            {
                return reinterpret_cast<T>(it->second);
            }

            void* handle = dlopen(path.c_str(), RTLD_LAZY);
            if (!handle)
            {
                return nullptr;
            }

            void* funcPtr = dlsym(handle, functionName.c_str());
            if (!funcPtr)
            {
                return nullptr;
            }

            functionCache[key] = funcPtr;
            return reinterpret_cast<T>(funcPtr);
        }
#endif
    };

    DynamicLibraryLoader _DynamicLibraryLoader;

#ifdef _WIN32
    std::wstring _libraryPath;
#else
    std::string _libraryPath;
#endif

public:
#ifndef _WIN32
    std::string GetHomeDirectory()
    {
    #ifdef __APPLE__
        struct passwd* pw = getpwuid(getuid());
        return pw ? pw->pw_dir : "";
    #else
        const char* home = getenv("HOME");
        return home ? home : "";
    #endif
    }
#endif

#ifdef _WIN32
    std::wstring SettingsPath()
#else
    std::string SettingsPath()
#endif
    {
        std::filesystem::path ret;

#ifdef _WIN32
        PWSTR pPath = NULL;
        if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &pPath) == S_OK)
        {
            ret = std::filesystem::path(pPath) / L"Looking Glass" / L"Bridge";
            CoTaskMemFree(pPath);
        }
#elif __APPLE__
        std::string home = GetHomeDirectory();
        ret = std::filesystem::path(home) / "Library" / "Application Support" / "Looking Glass" / "Bridge";
#else
        std::string home = GetHomeDirectory();
        ret = std::filesystem::path(home) / ".lgf" / "Bridge";
#endif

        ret /= "settings.json";

#ifdef _WIN32
    return ret.wstring();
#else
    return ret.string();
#endif    
    }

#ifdef _WIN32
std::wstring BridgeInstallLocation(const std::wstring& requestedVersion)
    {
        std::wstring settingsPath = SettingsPath();
        std::wifstream settingsFile(settingsPath);
        std::wstring ret = L"";
        if (settingsFile)
        {
            // Read file content into a wstring
            std::wstringstream buffer;
            buffer << settingsFile.rdbuf();
            std::wstring settingsContent = buffer.str();

            // Locate "install_locations" and the subsequent array
            size_t start = settingsContent.find(L"\"install_locations\":");
            if (start != std::wstring::npos)
            {
                start = settingsContent.find(L"[", start);
                if (start != std::wstring::npos)
                {
                    size_t end = settingsContent.find(L"]", start);
                    if (end != std::wstring::npos)
                    {
                        std::wstring arrayContent = settingsContent.substr(start + 1, end - start - 1);
                        std::vector<std::wstring> tokens;
                        size_t objStart = 0, objEnd = 0;

                        while ((objStart = arrayContent.find(L"{", objEnd)) != std::wstring::npos)
                        {
                            objEnd = arrayContent.find(L"}", objStart);
                            if (objEnd == std::wstring::npos) break;

                            std::wstring entry = arrayContent.substr(objStart, objEnd - objStart + 1);
                            tokens.push_back(entry);

                            objEnd++;
                        }

                        // Parse each token to build a map of versions to paths
                        std::map<std::wstring, std::wstring> versionPaths;
                        for (const std::wstring& token : tokens)
                        {
                            size_t versionKey = token.find(L"\"version\":");
                            size_t pathKey = token.find(L"\"path\":");
                            if (versionKey != std::wstring::npos && pathKey != std::wstring::npos)
                            {
                                size_t versionStart = token.find(L"\"", versionKey + 10) + 1;
                                size_t versionEnd = token.find(L"\"", versionStart);
                                std::wstring version = token.substr(versionStart, versionEnd - versionStart);

                                size_t pathStart = token.find(L"\"", pathKey + 7) + 1;
                                size_t pathEnd = token.find(L"\"", pathStart);
                                std::wstring path = token.substr(pathStart, pathEnd - pathStart);

                                versionPaths[version] = path;
                            }
                        }

                        // First, check if the requested version exists
                        if (versionPaths.find(requestedVersion) != versionPaths.end())
                        {
                            return versionPaths[requestedVersion];
                        }

                        // Find the highest version with the same major version number
                        std::wstring majorVersionOfRequested = requestedVersion.substr(0, requestedVersion.find(L'.'));
                        std::wstring highestVersion;
                        for (const auto& vp : versionPaths)
                        {
                            if (vp.first.substr(0, vp.first.find(L'.')) == majorVersionOfRequested)
                            {
                                if (highestVersion.empty() || vp.first > highestVersion)
                                {
                                    highestVersion = vp.first;
                                }
                            }
                        }

                        if (!highestVersion.empty())
                        {
                            return versionPaths[highestVersion];
                        }
                    }
                }
            }
        }
        return ret;  // Return an empty string if no matching version found
    }
#else
    std::string BridgeInstallLocation(const std::string& requestedVersion)
    {
        std::string settingsPath = SettingsPath();
        std::ifstream settingsFile(settingsPath);
        std::string ret = "";
        if (settingsFile)
        {
            // Read file content into a string
            std::stringstream buffer;
            buffer << settingsFile.rdbuf();
            std::string settingsContent = buffer.str();

            // Locate "install_locations" and the subsequent array
            size_t start = settingsContent.find("\"install_locations\":");
            if (start != std::string::npos)
            {
                start = settingsContent.find("[", start);
                if (start != std::string::npos)
                {
                    size_t end = settingsContent.find("]", start);
                    if (end != std::string::npos)
                    {
                        std::string arrayContent = settingsContent.substr(start + 1, end - start - 1);
                        std::vector<std::string> tokens;
                        size_t objStart = 0, objEnd = 0;

                        while ((objStart = arrayContent.find("{", objEnd)) != std::string::npos)
                        {
                            objEnd = arrayContent.find("}", objStart);
                            if (objEnd == std::string::npos) break;

                            std::string entry = arrayContent.substr(objStart, objEnd - objStart + 1);
                            tokens.push_back(entry);

                            objEnd++;
                        }

                        // Parse each token to build a map of versions to paths
                        std::map<std::string, std::string> versionPaths;
                        for (const std::string& token : tokens)
                        {
                            size_t versionKey = token.find("\"version\":");
                            size_t pathKey = token.find("\"path\":");
                            if (versionKey != std::string::npos && pathKey != std::string::npos)
                            {
                                size_t versionStart = token.find("\"", versionKey + 10) + 1;
                                size_t versionEnd = token.find("\"", versionStart);
                                std::string version = token.substr(versionStart, versionEnd - versionStart);

                                size_t pathStart = token.find("\"", pathKey + 7) + 1;
                                size_t pathEnd = token.find("\"", pathStart);
                                std::string path = token.substr(pathStart, pathEnd - pathStart);

                                versionPaths[version] = path;
                            }
                        }

                        // First, check if the requested version exists
                        if (versionPaths.find(requestedVersion) != versionPaths.end())
                        {
                            return versionPaths[requestedVersion];
                        }

                        // Find the highest version with the same major version number
                        std::string majorVersionOfRequested = requestedVersion.substr(0, requestedVersion.find('.'));
                        std::string highestVersion;
                        for (const auto& vp : versionPaths)
                        {
                            if (vp.first.substr(0, vp.first.find('.')) == majorVersionOfRequested)
                            {
                                if (highestVersion.empty() || vp.first > highestVersion)
                                {
                                    highestVersion = vp.first;
                                }
                            }
                        }

                        if (!highestVersion.empty())
                        {
                            return versionPaths[highestVersion];
                        }
                    }
                }
            }
        }
        return ret;  // Return an empty string if no matching version found
    }
#endif
   
#ifdef _WIN32
    bool Initialize(const std::wstring& app_name, const std::wstring& desired_bridge_version = ::BridgeVersion)
#else
    bool Initialize(const std::string& app_name, const std::string& desired_bridge_version = ::BridgeVersion)
#endif
    {
        auto installPath = BridgeInstallLocation(desired_bridge_version);
        return InitializeWithPath(app_name, installPath);
    }

#ifdef _WIN32
    bool InitializeWithPath(const std::wstring& app_name, const std::wstring& manual_install_location)
#else
    bool InitializeWithPath(const std::string& app_name, const std::string& manual_install_location)
#endif
    {
        if (manual_install_location.empty())
        {
            return false;
        }

#ifdef __APPLE__
        _libraryPath = (std::filesystem::path(manual_install_location) / "libbridge_inproc.dylib").string();
        try
        {
            auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(const char*)>(_libraryPath, "initialize_bridge");
        
            if (!func)
            {
                return false;
            }

            return func(app_name.c_str());
        }
        catch (const std::exception& ex)
        {
            std::cerr << "Error: " << ex.what() << std::endl;
            return false;
        }
#elif _WIN32
        SetDllDirectoryW(manual_install_location.c_str());
        _libraryPath = (std::filesystem::path(manual_install_location) / "bridge_inproc.dll").wstring();
        try
        {
            auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(const wchar_t*)>(_libraryPath, "initialize_bridge");

            if (!func)
            {
                return false;
            }

            return func(app_name.c_str());
        }
        catch (const std::exception& ex)
        {
            std::cerr << "Error: " << ex.what() << std::endl;
            return false;
        }
#else
        // todo: linux
#endif
        return false;
    }

    bool Uninitialize()
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)()>(_libraryPath, "uninitialize_bridge");
        
        if (!func)
        {
            return false;
        }

        return func();
    }

    bool GetBridgeVersion(unsigned long* major, unsigned long* minor, unsigned long* build, int* number_of_postfix_wchars, wchar_t* postfix) 
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(unsigned long*, unsigned long*, unsigned long*, int*, wchar_t*)>(_libraryPath, "get_bridge_version");
        
        if (!func)
        {
            return false;
        }

        return func(major, minor, build, number_of_postfix_wchars, postfix);
    }

    bool InstanceWindowGL(WINDOW_HANDLE* wnd, unsigned long head_index = static_cast<unsigned long>(FIRST_LOOKING_GLASS_DEVICE)) 
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE*, unsigned long)>(_libraryPath, "instance_window_gl");
        
        if (!func)
        {
            return false;
        }

        return func(wnd, head_index);
    }

    bool InstanceOffscreenWindow(WINDOW_HANDLE* wnd, unsigned long width, unsigned long height, const wchar_t* calibration_path) 
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE*, unsigned long, unsigned long, const wchar_t*)>(_libraryPath, "instance_offscreen_window");
        
        if (!func)
        {
            return false;
        }

        return func(wnd, width, height, calibration_path);
    }

    bool QuiltifyRGBD(WINDOW_HANDLE wnd, unsigned long columns, unsigned long rows, unsigned long views, float aspect, float zoom, float cam_dist, float fov, float crop_pos_x, float crop_pos_y, unsigned long depth_inversion, unsigned long chroma_depth, unsigned long depth_loc, float depthiness, float depth_cutoff, float focus, const wchar_t* input_path, const wchar_t* output_path) 
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, unsigned long, unsigned long, unsigned long, float, float, float, float, float, float, unsigned long, unsigned long, unsigned long, float, float, float, const wchar_t*, const wchar_t*)>(_libraryPath, "quiltify_rgbd");
        
        if (!func)
        {
            return false;
        }

        return func(wnd, columns, rows, views, aspect, zoom, cam_dist, fov, crop_pos_x, crop_pos_y, depth_inversion, chroma_depth, depth_loc, depthiness, depth_cutoff, focus, input_path, output_path);
    }

    bool GetWindowDimensions(WINDOW_HANDLE wnd, unsigned long* width, unsigned long* height) 
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, unsigned long*, unsigned long*)>(_libraryPath, "get_window_dimensions");
        
        if (!func)
        {
            return false;
        }

        return func(wnd, width, height);
    }

    bool GetMaxTextureSize(WINDOW_HANDLE wnd, unsigned long* size) 
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, unsigned long*)>(_libraryPath, "get_max_texture_size");
        
        if (!func)
        {
            return false;
        }

        return func(wnd, size);
    }

    bool SetInteropQuiltTextureGL(WINDOW_HANDLE wnd, unsigned long long texture, PixelFormats format, unsigned long width, unsigned long height, unsigned long vx, unsigned long vy, float aspect, float zoom) 
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, unsigned long long, PixelFormats, unsigned long, unsigned long, unsigned long, unsigned long, float, float)>(_libraryPath, "set_interop_quilt_texture_gl");
        
        if (!func)
        {
            return false;
        }

        return func(wnd, texture, format, width, height, vx, vy, aspect, zoom);
    }

    bool DrawInteropQuiltTextureGL(WINDOW_HANDLE wnd, unsigned long long texture, PixelFormats format, unsigned long width, unsigned long height, unsigned long vx, unsigned long vy, float aspect, float zoom) 
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, unsigned long long, PixelFormats, unsigned long, unsigned long, unsigned long, unsigned long, float, float)>(_libraryPath, "draw_interop_quilt_texture_gl");
        
        if (!func)
        {
            return false;
        }

        return func(wnd, texture, format, width, height, vx, vy, aspect, zoom);
    }

    bool ShowWindow(WINDOW_HANDLE wnd, bool flag) 
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, bool)>(_libraryPath, "show_window");
        
        if (!func)
        {
            return false;
        }

        return func(wnd, flag);
    }

    // File save functions differ by platform due to string type differences
    #ifdef WIN32
    bool SaveTextureToFileGL(WINDOW_HANDLE wnd, wchar_t* filename, unsigned long long texture, PixelFormats format, unsigned long width, unsigned long height) 
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, wchar_t*, unsigned long long, PixelFormats, unsigned long, unsigned long)>(_libraryPath, "save_texture_to_file_gl");
        
        if (!func)
        {
            return false;
        }

        return func(wnd, filename, texture, format, width, height);
    }

    bool SaveImageToFile(WINDOW_HANDLE wnd, wchar_t* filename, void* image, PixelFormats format, unsigned long width, unsigned long height) 
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, wchar_t*, void*, PixelFormats, unsigned long, unsigned long)>(_libraryPath, "save_image_to_file");
        
        if (!func)
        {
            return false;
        }

        return func(wnd, filename, image, format, width, height);
    }
    #else
    bool SaveTextureToFileGL(WINDOW_HANDLE wnd, char* filename, unsigned long long texture, PixelFormats format, unsigned long width, unsigned long height) 
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, char*, unsigned long long, PixelFormats, unsigned long, unsigned long)>(_libraryPath, "save_texture_to_file_gl");
        
        if (!func)
        {
            return false;
        }

        return func(wnd, filename, texture, format, width, height);
    }

    bool SaveImageToFile(WINDOW_HANDLE wnd, char* filename, void* image, PixelFormats format, unsigned long width, unsigned long height) 
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, char*, void*, PixelFormats, unsigned long, unsigned long)>(_libraryPath, "save_image_to_file");
        
        if (!func)
        {
            return false;
        }

        return func(wnd, filename, image, format, width, height);
    }
    #endif

    bool DeviceFromResourceDX(IUnknown * dx_resource, IUnknown **dx_device) 
    {
#ifndef _WIN32
        return false;
#endif

        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(IUnknown*, IUnknown**)>(_libraryPath, "device_from_resource_dx");
        
        if (!func)
        {
            return false;
        }

        return func(dx_resource, dx_device);
    }

    bool ReleaseDeviceDX(IUnknown * dx_device) 
    {
#ifndef _WIN32
        return false;
#endif

        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(IUnknown*)>(_libraryPath, "release_device_dx");
        
        if (!func)
        {
            return false;
        }

        return func(dx_device);
    }

    bool InstanceWindowDX(IUnknown *dx_device, WINDOW_HANDLE *wnd, unsigned long head_index = static_cast<unsigned long>(FIRST_LOOKING_GLASS_DEVICE)) 
    {
#ifndef _WIN32
        return false;
#endif

        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(IUnknown*, WINDOW_HANDLE*, unsigned long)>(_libraryPath, "instance_window_dx");
        
        if (!func)
        {
            return false;
        }

        return func(dx_device, wnd, head_index);
    }

    bool RegisterTextureDX(WINDOW_HANDLE wnd, IUnknown *dx_texture) 
    {
#ifndef _WIN32
        return false;
#endif
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, IUnknown*)>(_libraryPath, "register_texture_dx");
        
        if (!func)
        {
            return false;
        }

        return func(wnd, dx_texture);
    }

    bool UnregisterTextureDX(WINDOW_HANDLE wnd, IUnknown *dx_texture) 
    {
#ifndef _WIN32
        return false;
#endif
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, IUnknown*)>(_libraryPath, "unregister_texture_dx");
        
        if (!func)
        {
            return false;
        }

        return func(wnd, dx_texture);
    }

    bool SaveTextureToFileDX(WINDOW_HANDLE wnd, wchar_t* filename, IUnknown * dx_texture) 
    {
#ifndef _WIN32
        return false;
#endif
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, wchar_t*, IUnknown*)>(_libraryPath, "save_texture_to_file_dx");
        
        if (!func)
        {
            return false;
        }

        return func(wnd, filename, dx_texture);
    }

    bool DrawInteropQuiltTextureDX(WINDOW_HANDLE wnd, IUnknown * dx_texture, unsigned long vx, unsigned long vy, float aspect, float zoom) 
    {
#ifndef _WIN32
        return false;
#endif
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, IUnknown*, unsigned long, unsigned long, float, float)>(_libraryPath, "draw_interop_quilt_texture_dx");
        
        if (!func)
        {
            return false;
        }

        return func(wnd, dx_texture, vx, vy, aspect, zoom);
    }

    bool CreateTextureDX(WINDOW_HANDLE wnd, unsigned long width, unsigned long height, IUnknown **dx_texture) 
    {
#ifndef _WIN32
        return false;
#endif
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, unsigned long, unsigned long, IUnknown**)>(_libraryPath, "create_texture_dx");
        
        if (!func)
        {
            return false;
        }

        return func(wnd, width, height, dx_texture);
    }

    bool ReleaseTextureDX(WINDOW_HANDLE wnd, IUnknown *dx_texture) 
    {
#ifndef _WIN32
        return false;
#endif
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, IUnknown*)>(_libraryPath, "release_texture_dx");

        if (!func)
        {
            return false;
        }

        return func(wnd, dx_texture);
    }

    bool CopyTextureDX(WINDOW_HANDLE wnd, IUnknown *src, IUnknown *dest) 
    {
#ifndef _WIN32
        return false;
#endif
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, IUnknown*, IUnknown*)>(_libraryPath, "copy_texture_dx");

        if (!func)
        {
            return false;
        }

        return func(wnd, src, dest);
    }

    bool InstanceWindowMetal(void *metal_device, WINDOW_HANDLE *wnd, unsigned long head_index = static_cast<unsigned long>(FIRST_LOOKING_GLASS_DEVICE)) 
    {
#ifndef __APPLE__
        return false;
#endif
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(void*, WINDOW_HANDLE*, unsigned long)>(_libraryPath, "instance_window_metal");
        
        if (!func)
        {
            return false;
        }

        return func(metal_device, wnd, head_index);
    }

    bool CreateMetalTextureWithIOSurface(WINDOW_HANDLE wnd, void* descriptor, void** texture) 
    {
#ifndef __APPLE__
        return false;
#endif
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, void*, void**)>(_libraryPath, "create_metal_texture_with_iosurface");
        
        if (!func)
        {
            return false;
        }

        return func(wnd, descriptor, texture);
    }

    bool CopyMetalTexture(WINDOW_HANDLE wnd, void* src, void* dest) 
    {
#ifndef __APPLE__
        return false;
#endif
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, void*, void*)>(_libraryPath, "copy_metal_texture");
        
        if (!func)
        {
            return false;
        }

        return func(wnd, src, dest);
    }

    bool ReleaseMetalTexture(WINDOW_HANDLE wnd, void* texture) 
    {
#ifndef __APPLE__
        return false;
#endif
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, void*)>(_libraryPath, "release_metal_texture");
        
        if (!func)
        {
            return false;
        }

        return func(wnd, texture);
    }

    bool SaveMetalTextureToFile(WINDOW_HANDLE wnd, char* filename, void* texture, PixelFormats format, unsigned long width, unsigned long height) 
    {
#ifndef __APPLE__
        return false;
#endif
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, char*, void*, PixelFormats, unsigned long, unsigned long)>(_libraryPath, "save_metal_texture_to_file");
        
        if (!func)
        {
            return false;
        }

        return func(wnd, filename, texture, format, width, height);
    }

    bool DrawInteropQuiltTextureMetal(WINDOW_HANDLE wnd, void* texture, unsigned long vx, unsigned long vy, float aspect, float zoom) 
    {
#ifndef __APPLE__
        return false;
#endif
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, void*, unsigned long, unsigned long, float, float)>(_libraryPath, "draw_interop_quilt_texture_metal");
        
        if (!func)
        {
            return false;
        }

        return func(wnd, texture, vx, vy, aspect, zoom);
    }

    bool GetCalibration(WINDOW_HANDLE wnd, 
                    float* center, 
                    float* pitch, 
                    float* slope, 
                    int* width, 
                    int* height, 
                    float* dpi, 
                    float* flip_x,
                    int* invView,
                    float* viewcone,
                    float* fringe,
                    int* cell_pattern_mode,
                    int* number_of_cells,
                    CalibrationSubpixelCell* cells)
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, float*, float*, float*, int*, int*, float*, float*, int*, float*, float*, int*, int*, CalibrationSubpixelCell*)>(_libraryPath, "get_calibration");

        if (!func)
        {
            return false;
        }

        return func(wnd, center, pitch, slope, width, height, dpi, flip_x, invView, viewcone, fringe, cell_pattern_mode, number_of_cells, cells);
    }

    bool GetDeviceName(WINDOW_HANDLE wnd, int* number_of_device_name_wchars, wchar_t* device_name) 
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, int*, wchar_t*)>(_libraryPath, "get_device_name");
        
        if (!func)
        {
            return false;
        }

        return func(wnd, number_of_device_name_wchars, device_name);
    }

    bool GetDeviceSerial(WINDOW_HANDLE wnd, int* number_of_serial_wchars, wchar_t* serial) 
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, int*, wchar_t*)>(_libraryPath, "get_device_serial");
        
        if (!func)
        {
            return false;
        }

        return func(wnd, number_of_serial_wchars, serial);
    }

    bool GetDefaultQuiltSettings(WINDOW_HANDLE wnd, float* aspect, int* quilt_width, int* quilt_height, int* quilt_columns, int* quilt_rows) 
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, float*, int*, int*, int*, int*)>(_libraryPath, "get_default_quilt_settings");

        if (!func)
        {
            return false;
        }

        return func(wnd, aspect, quilt_width, quilt_height, quilt_columns, quilt_rows);
    }

    bool GetDisplays(int* number_of_indices, unsigned long* indices) 
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(int*, unsigned long*)>(_libraryPath, "get_displays");

        if (!func)
        {
            return false;
        }

        return func(number_of_indices, indices);
    }

    bool GetDeviceNameForDisplay(unsigned long display_index, int* number_of_device_name_wchars, wchar_t* device_name) 
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(unsigned long, int*, wchar_t*)>(_libraryPath, "get_device_name_for_display");

        if (!func)
        {
            return false;
        }

        return func(display_index, number_of_device_name_wchars, device_name);
    }

    bool GetDeviceSerialForDisplay(unsigned long display_index, int* number_of_serial_wchars, wchar_t* serial) 
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(unsigned long, int*, wchar_t*)>(_libraryPath, "get_device_serial_for_display");

        if (!func)
        {
            return false;
        }

        return func(display_index, number_of_serial_wchars, serial);
    }

    bool GetDimensionsForDisplay(unsigned long display_index, unsigned long* width, unsigned long* height) 
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(unsigned long, unsigned long*, unsigned long*)>(_libraryPath, "get_dimensions_for_display");

        if (!func)
        {
            return false;
        }

        return func(display_index, width, height);
    }

    bool GetDeviceTypeForDisplay(unsigned long display_index, int* hw_enum) 
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(unsigned long, int*)>(_libraryPath, "get_device_type_for_display");

        if (!func)
        {
            return false;
        }

        return func(display_index, hw_enum);
    }

    bool GetCalibrationForDisplay(unsigned long display_index, 
                              float* center, 
                              float* pitch, 
                              float* slope, 
                              int* width, 
                              int* height, 
                              float* dpi, 
                              float* flip_x,
                              int* invView,
                              float* viewcone,
                              float* fringe,
                              int* cell_pattern_mode,
                              int* number_of_cells,
                              CalibrationSubpixelCell* cells)
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(unsigned long, float*, float*, float*, int*, int*, float*, float*, int*, float*, float*, int*, int*, CalibrationSubpixelCell*)>(_libraryPath, "get_calibration_for_display");

        if (!func)
        {
            return false;
        }

        return func(display_index, center, pitch, slope, width, height, dpi, flip_x, invView, viewcone, fringe, cell_pattern_mode, number_of_cells, cells);
    }


    bool GetInvViewForDisplay(unsigned long display_index, int* invview)
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(unsigned long, int*)>(_libraryPath, "get_invview_for_display");

        if (!func)
        {
            return false;
        }

        return func(display_index, invview);
    }

    bool GetRiForDisplay(unsigned long display_index, int* ri)
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(unsigned long, int*)>(_libraryPath, "get_ri_for_display");

        if (!func)
        {
            return false;
        }

        return func(display_index, ri);
    }

    bool GetBiForDisplay(unsigned long display_index, int* bi)
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(unsigned long, int*)>(_libraryPath, "get_bi_for_display");

        if (!func)
        {
            return false;
        }

        return func(display_index, bi);
    }

    bool GetTiltForDisplay(unsigned long display_index, float* tilt)
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(unsigned long, float*)>(_libraryPath, "get_tilt_for_display");

        if (!func)
        {
            return false;
        }

        return func(display_index, tilt);
    }

    bool GetDisplayAspectForDisplay(unsigned long display_index, float* displayaspect)
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(unsigned long, float*)>(_libraryPath, "get_displayaspect_for_display");

        if (!func)
        {
            return false;
        }

        return func(display_index, displayaspect);
    }

    bool GetFringeForDisplay(unsigned long display_index, float* fringe)
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(unsigned long, float*)>(_libraryPath, "get_fringe_for_display");

        if (!func)
        {
            return false;
        }

        return func(display_index, fringe);
    }

    bool GetSubpForDisplay(unsigned long display_index, float* subp)
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(unsigned long, float*)>(_libraryPath, "get_subp_for_display");

        if (!func)
        {
            return false;
        }

        return func(display_index, subp);
    }

    bool GetViewConeForDisplay(unsigned long display_index, float* viewcone)
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(unsigned long, float*)>(_libraryPath, "get_viewcone_for_display");

        if (!func)
        {
            return false;
        }

        return func(display_index, viewcone);
    }

    bool GetDisplayForWindow(WINDOW_HANDLE wnd, unsigned long* display_index)
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, unsigned long*)>(_libraryPath, "get_display_for_window");

        if (!func)
        {
            return false;
        }

        return func(wnd, display_index);
    }

    bool GetDefaultQuiltSettingsForDisplay(unsigned long display_index, float* aspect, int* quilt_width, int* quilt_height, int* quilt_columns, int* quilt_rows)
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(unsigned long, float*, int*, int*, int*, int*)>(_libraryPath, "get_default_quilt_settings_for_display");

        if (!func)
        {
            return false;
        }

        return func(display_index, aspect, quilt_width, quilt_height, quilt_columns, quilt_rows);
    }

    bool GetDeviceType(WINDOW_HANDLE wnd, int* hw_enum)
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, int*)>(_libraryPath, "get_device_type");

        if (!func)
        {
            return false;
        }

        return func(wnd, hw_enum);
    }

    bool GetPitchForDisplay(unsigned long display_index, float* pitch)
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(unsigned long, float*)>(_libraryPath, "get_pitch_for_display");

        if (!func)
        {
            return false;
        }

        return func(display_index, pitch);
    }

    bool GetCenterForDisplay(unsigned long display_index, float* center)
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(unsigned long, float*)>(_libraryPath, "get_center_for_display");

        if (!func)
        {
            return false;
        }

        return func(display_index, center);
    }

    bool GetViewCone(WINDOW_HANDLE wnd, float* viewcone)
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, float*)>(_libraryPath, "get_viewcone");

        if (!func)
        {
            return false;
        }

        return func(wnd, viewcone);
    }

    bool GetInvView(WINDOW_HANDLE wnd, int* invview)
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, int*)>(_libraryPath, "get_invview");

        if (!func)
        {
            return false;
        }

        return func(wnd, invview);
    }

    bool GetRi(WINDOW_HANDLE wnd, int* ri)
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, int*)>(_libraryPath, "get_ri");

        if (!func)
        {
            return false;
        }

        return func(wnd, ri);
    }

    bool GetBi(WINDOW_HANDLE wnd, int* bi)
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, int*)>(_libraryPath, "get_bi");

        if (!func)
        {
            return false;
        }

        return func(wnd, bi);
    }

    bool GetTilt(WINDOW_HANDLE wnd, float* tilt)
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, float*)>(_libraryPath, "get_tilt");

        if (!func)
        {
            return false;
        }

        return func(wnd, tilt);
    }

    bool GetDisplayAspect(WINDOW_HANDLE wnd, float* displayaspect)
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, float*)>(_libraryPath, "get_displayaspect");

        if (!func)
        {
            return false;
        }

        return func(wnd, displayaspect);
    }

    bool GetFringe(WINDOW_HANDLE wnd, float* fringe)
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, float*)>(_libraryPath, "get_fringe");

        if (!func)
        {
            return false;
        }

        return func(wnd, fringe);
    }

    bool GetSubp(WINDOW_HANDLE wnd, float* subp)
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, float*)>(_libraryPath, "get_subp");

        if (!func)
        {
            return false;
        }

        return func(wnd, subp);
    }

    bool GetPitch(WINDOW_HANDLE wnd, float* pitch)
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, float*)>(_libraryPath, "get_pitch");

        if (!func)
        {
            return false;
        }

        return func(wnd, pitch);
    }

    bool GetCenter(WINDOW_HANDLE wnd, float* center)
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, float*)>(_libraryPath, "get_center");

        if (!func)
        {
            return false;
        }

        return func(wnd, center);
    }

    bool GetWindowPosition(WINDOW_HANDLE wnd, long* x, long* y)
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, long*, long*)>(_libraryPath, "get_window_position");

        if (!func)
        {
            return false;
        }

        return func(wnd, x, y);
    }

    bool GetWindowPositionForDisplay(unsigned long display_index, long* x, long* y)
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(unsigned long, long*, long*)>(_libraryPath, "get_window_position_for_display");

        if (!func)
        {
            return false;
        }

        return func(display_index, x, y);
    }
};

#ifdef _WIN32
#pragma warning(default : 4244)
#endif
