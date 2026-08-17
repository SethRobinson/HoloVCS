#pragma once
#include "bridge.h"

extern "C" INTEROP_EXPORT bool set_calibration(WINDOW_HANDLE wnd, 
	                                           float center, 
	                                           float pitch, 
	                                           float slope, 
	                                           int width, 
	                                           int height, 
	                                           float dpi, 
	                                           float flip_x,
                                               int invView,
									           float viewcone,
									           float fringe,
                                               int cell_pattern_mode,
											   int number_of_cells,
	                                           CalibrationSubpixelCell* cells);

extern "C" INTEROP_EXPORT bool get_calibration_template_count(int* template_count);

// mlc: call twice -- first set config_version = nullptr, it will return the other params.
// Then alloc the config_version array on the client side with the apropos size and call again with that pointer.
extern "C" INTEROP_EXPORT bool get_calibration_template_config_version(int template_index, int* number_of_config_wchars, wchar_t* config_version);

// mlc: call twice -- first set device_name = nullptr, it will return the other params.
// Then alloc the device_name array on the client side with the apropos size and call again with that pointer.
extern "C" INTEROP_EXPORT bool get_calibration_template_device_name(int template_index, int* number_of_device_name_wchars, wchar_t* device_name);

// mlc: call twice -- first set serial = nullptr, it will return the other params.
// Then alloc the serial array on the client side with the apropos size and call again with that pointer.
extern "C" INTEROP_EXPORT bool get_calibration_template_serial(int template_index, int* number_of_serial_wchars, wchar_t* serial);

// mlc: call twice -- first set cells = nullptr, it will return the other params.
// Then alloc the SubpixelCell array on the client side with the apropos size and call again with that pointer.
extern "C" INTEROP_EXPORT bool get_calibration_template(int template_index, 
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


class ControllerWithCalibrationTemplates : public Controller
{
public:
	bool GetCalibrationTemplateCount(int* template_count) 
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(int*)>(_libraryPath, "get_calibration_template_count");
        
        if (!func)
        {
            return false;
        }

        return func(template_count);
    }

    bool GetCalibrationTemplateConfigVersion(int template_index, int* number_of_config_wchars, wchar_t* config_version) 
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(int, int*, wchar_t*)>(_libraryPath, "get_calibration_template_config_version");
        
        if (!func)
        {
            return false;
        }

        return func(template_index, number_of_config_wchars, config_version);
    }

    bool GetCalibrationTemplateDeviceName(int template_index, int* number_of_device_name_wchars, wchar_t* device_name) 
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(int, int*, wchar_t*)>(_libraryPath, "get_calibration_template_device_name");
        
        if (!func)
        {
            return false;
        }

        return func(template_index, number_of_device_name_wchars, device_name);
    }

    bool GetCalibrationTemplateSerial(int template_index, int* number_of_serial_wchars, wchar_t* serial) 
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(int, int*, wchar_t*)>(_libraryPath, "get_calibration_template_serial");
        
        if (!func)
        {
            return false;
        }

        return func(template_index, number_of_serial_wchars, serial);
    }

    bool GetCalibrationTemplate(int template_index, 
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
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(int, float*, float*, float*, int*, int*, float*, float*, int*, float*, float*, int*, int*, CalibrationSubpixelCell*)>(_libraryPath, "get_calibration_template");

        if (!func)
        {
            return false;
        }

        return func(template_index, center, pitch, slope, width, height, dpi, flip_x, invView, viewcone, fringe, cell_pattern_mode, number_of_cells, cells);
    }

    bool SetCalibration(WINDOW_HANDLE wnd, 
                        float center, 
                        float pitch, 
                        float slope, 
                        int width, 
                        int height, 
                        float dpi, 
                        float flip_x,
                        int invView,
                        float viewcone,
                        float fringe,
                        int cell_pattern_mode,
                        int number_of_cells,
                        CalibrationSubpixelCell* cells)
    {
        auto func = _DynamicLibraryLoader.LoadFunction<bool(*)(WINDOW_HANDLE, float, float, float, int, int, float, float, int, float, float, int, int, CalibrationSubpixelCell*)>(_libraryPath, "set_calibration");

        if (!func)
        {
            return false;
        }

        return func(wnd, center, pitch, slope, width, height, dpi, flip_x, invView, viewcone, fringe, cell_pattern_mode, number_of_cells, cells);
    }
};