// Copyright (C) 2021 - 2022 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// interface for generation of stockham kernels

#pragma once
#include "../../../../shared/arithmetic.h"
#include "rocfft.h"
#include <string>
#include <vector>

struct StockhamGeneratorSpecs
{
    StockhamGeneratorSpecs(const std::vector<unsigned int>& factors,
                           const std::vector<unsigned int>& factors2d,
                           const std::vector<unsigned int>& precisions,
                           unsigned int                     workgroup_size,
                           const std::string&               scheme)
        : factors(factors)
        , factors2d(factors2d)
        , precisions(precisions)
        , length(product(factors.begin(), factors.end()))
        , length2d(product(factors2d.begin(), factors2d.end()))
        , workgroup_size(workgroup_size)
        , scheme(scheme)
    {
    }

    std::vector<unsigned int> factors;
    std::vector<unsigned int> factors2d;
    std::vector<unsigned int> precisions; // mapped from rocfft_precision
    unsigned int              length;
    unsigned int              length2d = 0;

    unsigned int workgroup_size;
    unsigned int threads_per_transform = 0;
    bool         half_lds              = false;
    bool         direct_to_from_reg    = false;
    // dimension of the kernel - 0 if the generated kernel accepts a
    // 'dim' argument at runtime; otherwise the dimension is
    // statically defined for the kernel
    unsigned int static_dim = 0;
    std::string  scheme;
};

// generate default stockham variants for ahead-of-time compilation
std::string stockham_variants(const std::string&      filename,
                              StockhamGeneratorSpecs& specs,
                              StockhamGeneratorSpecs& specs2d);
