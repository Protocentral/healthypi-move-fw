/* Edge Impulse inferencing library
 * Copyright (c) 2023 EdgeImpulse Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef EI_CLASSIFIER_INFERENCING_ENGINE_MEMRYX_H
#define EI_CLASSIFIER_INFERENCING_ENGINE_MEMRYX_H

#if (EI_CLASSIFIER_INFERENCING_ENGINE == EI_CLASSIFIER_MEMRYX)

/**
 * @brief we are forcing SOFTWARE inference (simulation),
 *        beacuse use of hardware is not ready
 *
 */
#ifndef EI_CLASSIFIER_USE_MEMRYX_SOFTWARE
#define EI_CLASSIFIER_USE_MEMRYX_SOFTWARE 1
#define EI_CLASSIFIER_USE_MEMRYX_HARDWARE 0
#endif

/**
 * @brief Memryx accelerator can leverage up to four MX3 chips for inference.
 *        Specify here the number of chips to be used for acceleration,
 *        e.g. set to 4 in order to use all four chips of the M3X board.
 */
#ifndef EI_CLASSIFIER_USE_MEMRYX_CHIPS_COUNT
#define EI_CLASSIFIER_USE_MEMRYX_CHIPS_COUNT 1
#endif

#include "model-parameters/model_metadata.h"
#if EI_CLASSIFIER_HAS_MODEL_VARIABLES == 1
#include "model-parameters/model_variables.h"
#endif
/* TODO: Not sure we need it #include <memryx_model/memryx_model.h> */
#include "edge-impulse-sdk/porting/ei_classifier_porting.h"
#include "edge-impulse-sdk/classifier/ei_fill_result_struct.h"
#include "edge-impulse-sdk/tensorflow/lite/kernels/internal/reference/softmax.h"
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <limits>
#include <math.h>
#include "pybind11/embed.h"
#include "pybind11/numpy.h"
#include "pybind11/stl.h"

/* Result delivered by memryx simulator contains 3 fields, indexes for print */
#define MX_SIM_RES_OUTPUTS 0
#define MX_SIM_RES_LATENCY 1
#define MX_SIM_RES_FPS     2

/* brings in the `_a` literals to set args to python API */
using namespace pybind11::literals;

namespace py = pybind11;

std::stringstream engine_info;

static py::module_ memryx;
static py::module_ np;
static py::object zeroes;
static py::object Simulator;
static py::object model;
static py::object device;
static bool memryx_initialized = false;
static std::vector<size_t> vec;
/* We need a workaround for softmax because
 * the MX3+ is not coming out this year, and
 * the MX3 does not support the SoftMax layer
 */
static tflite::RuntimeShape softmax_shape;
static tflite::SoftmaxParams dummy_params;

bool init_memryx(bool debug)
{
    py::list path;
    constexpr char model_file_path[] = "memryx_trained.dfp";

#if (defined(EI_CLASSIFIER_USE_MEMRYX_HARDWARE) && (EI_CLASSIFIER_USE_MEMRYX_HARDWARE == 1))
#warning "Trying to use hardware"
    /* TODO Use C++ Driver to run on the MX3 board
     * Deploy memryx model file into temporary file
    std::ofstream model_file(model_file_path, std::ios::out | std::ios::binary);
    model_file.write(reinterpret_cast<const char*>(memryx_model_fbz), memryx_model_fbz_len);
    if(model_file.bad()) {
        ei_printf("ERR: failed to unpack model ile into %s\n", model_file_path);
        model_file.close();
        return false;
    }
    model_file.close();
     */
#elif (defined(EI_CLASSIFIER_USE_MEMRYX_SOFTWARE) && (EI_CLASSIFIER_USE_MEMRYX_SOFTWARE == 1))
#warning "MEMRYX model will be run in SIMULATION mode (not on real hardware)!"
    // import Python's memryx module
    try {
        memryx = py::module_::import("memryx");
        if(debug) printf("Memryx PyModule init\n");
    }
    catch (py::error_already_set &e) {
        ei_printf("ERR: Importing 'memryx' library failed:\n%s\n", e.what());
        return false;
    }

    Simulator = memryx.attr("Simulator");
    if(debug) printf("Simulator API init\n");

    // load model
    try {
        model = Simulator("dfp"_a = model_file_path);
        if(debug) printf("Model API init\n");
    }
    catch (py::error_already_set &e) {
        ei_printf("ERR: Can't load model file from %s\n", model_file_path);
        return false;
    }
#else
#error "Neither EI_CLASSIFIER_USE_MEMRYX_HARDWARE or EI_CLASSIFIER_USE_MEMRYX_SOFTWARE are defined or set to 1"
#endif

    // clear info
    engine_info.str("");

    return true;
}


/**
 * @brief      Do neural network inferencing over the processed feature matrix
 *
 * @param      impulse  Struct describing impulse architecture
 * @param      fmatrix  Processed matrix
 * @param      result   Output classifier results
 * @param[in]  debug    Debug output enable
 *
 * @return     The ei impulse error.
 */
#if (defined(EI_CLASSIFIER_USE_MEMRYX_HARDWARE) && (EI_CLASSIFIER_USE_MEMRYX_HARDWARE == 1))
#warning "About to run inference on MX3 hardware"
#elif (defined(EI_CLASSIFIER_USE_MEMRYX_SOFTWARE) && (EI_CLASSIFIER_USE_MEMRYX_SOFTWARE == 1))
#warning "About to run inference using Memryx simulator"
EI_IMPULSE_ERROR run_nn_inference(
    const ei_impulse_t *impulse,
    ei::matrix_t *fmatrix,
    ei_impulse_result_t *result,
    void *config_ptr,
    bool debug = false)
{
    // init Python embedded interpreter (should be called once!)
    static py::scoped_interpreter guard{};

    // check if we've initialized the interpreter and device?
    if (memryx_initialized == false) {
        if(init_memryx(debug) == false) {
            return EI_IMPULSE_MEMRYX_ERROR;
        }
        memryx_initialized = true;
    }

    std::vector<size_t> input_shape = {1,EI_CLASSIFIER_INPUT_WIDTH,EI_CLASSIFIER_INPUT_HEIGHT,3};
    py::array_t<float> input_data(input_shape); // = zeroes(input_shape, 0);

    printf("impulse->w=%d h=%d\n", impulse->input_width, impulse->input_height);

    /*
     * convert features data to the expected shape (4dim)
     * For images RGB shape is (width, height, colors)
     * For images BW shape is (width, height, 1)
     * For Audio shape is (width, height, 1) - spectrogram
     */
    auto r = input_data.mutable_unchecked<4>();
    for (py::ssize_t x = 0; x < r.shape(1); x++) {
        for (py::ssize_t y = 0; y < r.shape(2); y++) {
            for(py::ssize_t z = 0; z < r.shape(3); z++) {
                r(0, x, y, z) = (float)(fmatrix->buffer[x * r.shape(2) * r.shape(3) + y * r.shape(3) + z]);
            }
        }
    }

    py::object runmodel = model.attr("run");
    // result from mx_sim is {np array, float, float}
    py::tuple args = py::make_tuple(py::none(), 0.00, 0.00);
    // run inference in sumualtor
    printf("start inference\n");
    uint64_t ctx_start_us = ei_read_timer_us();
    args = runmodel("inputs"_a=input_data,"frames"_a=1);
    uint64_t ctx_end_us = ei_read_timer_us();
    printf("end of inference\n");

    result->timing.classification_us = ctx_end_us - ctx_start_us;
    result->timing.classification = (int)(result->timing.classification_us / 1000);

    engine_info.str("");
    engine_info << "Inferences per second: " << (1000000 / result->timing.classification_us);

    py::array outputs = py::list(args[0]);
    py::array_t<float> potentials;
    std::vector<float> potentials_v;

    potentials = outputs.squeeze().cast<py::array_t<float>>();

    if (impulse->object_detection == false) {
        potentials_v = outputs.squeeze().cast<std::vector<float>>();
    }
    else {
        auto q = potentials.unchecked<>();
        for (py::ssize_t x = 0; x < q.shape(0); x++) {
            for (py::ssize_t y = 0; y < q.shape(1); y++) {
                for(py::ssize_t z = 0; z < q.shape(2); z++) {
                    potentials_v.push_back(q(x, y, z));
                }
            }
        }
    }

    if(debug) {
        std::string ret_str = py::str(potentials).cast<std::string>();
        ei_printf("Memryx raw output:\n%s\n", ret_str.c_str());
    }

    if (impulse->object_detection) {
        switch (impulse->object_detection_last_layer) {
            case EI_CLASSIFIER_LAST_LAYER_FOMO: {
                ei_printf("FOMO executed on Memryx\n");
                fill_result_struct_f32_fomo(
                    impulse,
                    result,
                    potentials_v.data(),
                    impulse->input_width / 8,
                    impulse->input_height / 8);
                break;
            }
            case EI_CLASSIFIER_LAST_LAYER_SSD: {
                ei_printf("Mobilenet SSD executed on Memryx\n");
                break;
            }
            default: {
                ei_printf("ERR: Unsupported object detection last layer (%d)\n",
                    impulse->object_detection_last_layer);
                return EI_IMPULSE_UNSUPPORTED_INFERENCING_ENGINE;
            }
        }
    }
    else {
        fill_result_struct_f32(impulse, result, potentials_v.data(), debug);
    }

    return EI_IMPULSE_OK;
}
#else
#error "Neither EI_CLASSIFIER_USE_MEMRYX_HARDWARE or EI_CLASSIFIER_USE_MEMRYX_SOFTWARE are defined or set to 1"
#endif // USE_HARDWARE

#endif // EI_CLASSIFIER_INFERENCING_ENGINE == EI_CLASSIFIER_MEMRYX

#endif /* EI_CLASSIFIER_INFERENCING_ENGINE_MEMRYX_H */
