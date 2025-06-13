/*******************************************************************************
* Copyright 2019-2023 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#ifndef COMMON_BFLOAT16_HPP
#define COMMON_BFLOAT16_HPP

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <type_traits>

#include "bit_cast.hpp"

//#include "oneapi/dnnl/dnnl.h"

namespace dnnl {
namespace impl {

// #if DNNL_CPU_RUNTIME != DNNL_RUNTIME_NONE
// struct bfloat16_t;
// bool try_cvt_float_to_bfloat16(bfloat16_t *out, const float *inp);
// #endif

struct bfloat16_t {
    uint16_t raw_bits_;
    bfloat16_t() = default;
    constexpr bfloat16_t(uint16_t r, bool) : raw_bits_(r) {}
    bfloat16_t(float f) { (*this) = f; }

    template <typename IntegerType,
            typename SFINAE = typename std::enable_if<
                    std::is_integral<IntegerType>::value>::type>
    bfloat16_t(const IntegerType i)
        : raw_bits_ {convert_bits_of_normal_or_zero(
                utils::bit_cast<uint32_t>(static_cast<float>(i)))} {}

    bfloat16_t &operator=(float f) {
        // #if DNNL_CPU_RUNTIME != DNNL_RUNTIME_NONE
        // if (try_cvt_float_to_bfloat16(this, &f)) { return *this; }
        // #endif
        auto iraw = utils::bit_cast<std::array<uint16_t, 2>>(f);
        switch (std::fpclassify(f)) {
                case FP_SUBNORMAL:
                case FP_ZERO:
                // sign preserving zero (denormal go to zero)
                raw_bits_ = iraw[1];
                raw_bits_ &= 0x8000;
                break;
                case FP_INFINITE: raw_bits_ = iraw[1]; break;
                case FP_NAN:
                // truncate and set MSB of the mantissa force QNAN
                raw_bits_ = iraw[1];
                raw_bits_ |= 1 << 6;
                break;
                case FP_NORMAL:
                // round to nearest even and truncate
                const uint32_t rounding_bias = 0x00007FFF + (iraw[1] & 0x1);
                const uint32_t int_raw
                        = utils::bit_cast<uint32_t>(f) + rounding_bias;
                iraw = utils::bit_cast<std::array<uint16_t, 2>>(int_raw);
                raw_bits_ = iraw[1];
                break;
        }

        return *this;
        }

    template <typename IntegerType,
            typename SFINAE = typename std::enable_if<
                    std::is_integral<IntegerType>::value>::type>
    bfloat16_t &operator=(const IntegerType i) {
        // Call the converting constructor that is optimized for integer types,
        // followed by the fast defaulted move-assignment operator.
        return (*this) = bfloat16_t {i};
    }

    operator float() const {
        std::array<uint16_t, 2> iraw = {{0, raw_bits_}};
        return utils::bit_cast<float>(iraw);
    }

    bfloat16_t &operator+=(const float a) {
        (*this) = float {*this} + a;
        return *this;
    }

private:
    // Converts the 32 bits of a normal float or zero to the bits of a bfloat16.
    static constexpr uint16_t convert_bits_of_normal_or_zero(
            const uint32_t bits) {
        return static_cast<uint16_t>(
                uint32_t {bits
                        + uint32_t {0x7FFFU + (uint32_t {bits >> 16} & 1U)}}
                >> 16);
    }
};

static_assert(sizeof(bfloat16_t) == 2, "bfloat16_t must be 2 bytes");

void cvt_float_to_bfloat16(bfloat16_t *out, const float *inp, size_t nelems);
void cvt_bfloat16_to_float(float *out, const bfloat16_t *inp, size_t nelems);

// performs element-by-element sum of inp and add float arrays and stores
// result to bfloat16 out array with downconversion
// out[:] = (bfloat16_t)(inp0[:] + inp1[:])
void add_floats_and_cvt_to_bfloat16(
        bfloat16_t *out, const float *inp0, const float *inp1, size_t nelems);

} // namespace impl
} // namespace dnnl

#endif
