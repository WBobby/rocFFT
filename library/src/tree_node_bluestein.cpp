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

#include "tree_node_bluestein.h"
#include "function_pool.h"
#include "kernel_launch.h"
#include "node_factory.h"
#include <numeric>

inline size_t FindBlue(size_t len, rocfft_precision precision, bool forcePow2)
{
    if(forcePow2)
    {
        size_t p = 1;
        while(p < len)
            p <<= 1;
        return 2 * p;
    }

    size_t lenPow2 = 1;
    while(lenPow2 < len)
        lenPow2 <<= 1;

    size_t minLenBlue  = 2 * len - 1;
    size_t length      = minLenBlue;
    size_t lenPow2Blue = 2 * lenPow2;

    // We don't want to choose a non-pow2 length that is too close to lenPow2Blue,
    // otherwise using a non-pow2 length may end up being slower than using lenPow2Blue.
    // This ratio has been experimentally verified to yield a non-pow2 length that is at
    // least as fast as its corresponding pow2 length.
    double lenCutOffRatio = .9;

    for(length = minLenBlue; length < lenPow2Blue; ++length)
    {
        auto lenSupported = NodeFactory::NonPow2LengthSupported(precision, length);
        auto lenRatio     = static_cast<double>(length) / static_cast<double>(lenPow2Blue);

        if(lenSupported && lenRatio < lenCutOffRatio)
            break;
    }

    return length;
}

/*****************************************************
 * CS_BLUESTEIN
 *****************************************************/
void BluesteinNode::BuildTree_internal()
{
    bool useSingleKernel = BluesteinSingleNode::SizeFits(length[0], precision);

    // Build a node for a 1D stage using the Bluestein algorithm for
    // general transform lengths.

    // single kernel sticks to pow2 lengthBlue.  the kernel does many
    // other things besides FFTs, so keep radices simple to reduce
    // VGPR usage.
    lengthBlue = FindBlue(length[0], precision, useSingleKernel);

    auto chirpPlan       = NodeFactory::CreateNodeFromScheme(CS_KERNEL_CHIRP, this);
    chirpPlan->dimension = 1;
    chirpPlan->length.push_back(length[0]);
    chirpPlan->lengthBlue = lengthBlue;
    chirpPlan->direction  = direction;
    chirpPlan->batch      = 1;
    chirpPlan->large1D    = 2 * length[0];

    // single kernel requires a single lengthBlue FFT on the second
    // half of chirp buffer before we do the rest of the Bluestein
    // steps that kernel
    if(useSingleKernel)
    {
        NodeMetaData chirpFFTPlanData(this);
        chirpFFTPlanData.dimension = 1;
        chirpFFTPlanData.length.push_back(lengthBlue);
        chirpFFTPlanData.batch   = 1;
        chirpFFTPlanData.iOffset = lengthBlue;
        chirpFFTPlanData.oOffset = lengthBlue;
        auto chirpFFTPlan        = NodeFactory::CreateExplicitNode(chirpFFTPlanData, this);
        chirpFFTPlan->RecursiveBuildTree();

        auto singlePlan       = NodeFactory::CreateNodeFromScheme(CS_KERNEL_BLUESTEIN_SINGLE, this);
        singlePlan->dimension = 1;
        singlePlan->length    = length;
        singlePlan->lengthBlue = lengthBlue;

        childNodes.emplace_back(std::move(chirpPlan));
        childNodes.emplace_back(std::move(chirpFFTPlan));
        childNodes.emplace_back(std::move(singlePlan));
    }
    else
    {
        // otherwise, use multiple kernels for all the Bluestein steps

        auto padmulPlan        = NodeFactory::CreateNodeFromScheme(CS_KERNEL_PAD_MUL, this);
        padmulPlan->dimension  = 1;
        padmulPlan->length     = length;
        padmulPlan->lengthBlue = lengthBlue;

        NodeMetaData ffticPlanData(this);
        ffticPlanData.dimension = 1;
        ffticPlanData.length.push_back(lengthBlue);
        ffticPlanData.batch
            *= std::accumulate(length.begin() + 1, length.end(), 1, std::multiplies<size_t>());
        ffticPlanData.batch++;
        ffticPlanData.iOffset = lengthBlue;
        ffticPlanData.oOffset = lengthBlue;
        auto ffticPlan        = NodeFactory::CreateExplicitNode(ffticPlanData, this);
        // FFT nodes must be in-place - were FFTing the second half
        // of chirp as well as the padded user data (via iOffset,
        // oOffset), so if the result goes to a different temp buffer
        // we lose the offset information.
        ffticPlan->allowOutofplace = false;
        ffticPlan->RecursiveBuildTree();

        auto fftmulPlan       = NodeFactory::CreateNodeFromScheme(CS_KERNEL_FFT_MUL, this);
        fftmulPlan->dimension = 1;
        fftmulPlan->length.push_back(lengthBlue);
        for(size_t index = 1; index < length.size(); index++)
        {
            fftmulPlan->length.push_back(length[index]);
        }
        fftmulPlan->lengthBlue = lengthBlue;

        NodeMetaData fftrPlanData(this);
        fftrPlanData.dimension = 1;
        fftrPlanData.length.push_back(lengthBlue);
        for(size_t index = 1; index < length.size(); index++)
        {
            fftrPlanData.length.push_back(length[index]);
        }
        fftrPlanData.direction    = -direction;
        fftrPlanData.iOffset      = 2 * lengthBlue;
        fftrPlanData.oOffset      = 2 * lengthBlue;
        auto fftrPlan             = NodeFactory::CreateExplicitNode(fftrPlanData, this);
        fftrPlan->allowOutofplace = false;
        fftrPlan->RecursiveBuildTree();

        auto resmulPlan        = NodeFactory::CreateNodeFromScheme(CS_KERNEL_RES_MUL, this);
        resmulPlan->dimension  = 1;
        resmulPlan->length     = length;
        resmulPlan->lengthBlue = lengthBlue;

        childNodes.emplace_back(std::move(chirpPlan));
        childNodes.emplace_back(std::move(padmulPlan));
        childNodes.emplace_back(std::move(ffticPlan));
        childNodes.emplace_back(std::move(fftmulPlan));
        childNodes.emplace_back(std::move(fftrPlan));
        childNodes.emplace_back(std::move(resmulPlan));
    }
}

void BluesteinNode::AssignParams_internal()
{
    // should either be in a 3-kernel BLUESTEIN_SINGLE plan, or a
    // 6-kernel multi-kernel Bluestein plan
    if(childNodes.size() == 3)
    {
        auto& chirpPlan    = childNodes[0];
        auto& chirpFFTPlan = childNodes[1];
        auto& singlePlan   = childNodes[2];

        chirpPlan->inStride.push_back(1);
        chirpPlan->iDist = chirpPlan->lengthBlue;
        chirpPlan->outStride.push_back(1);
        chirpPlan->oDist = chirpPlan->lengthBlue;

        chirpFFTPlan->inStride  = chirpPlan->outStride;
        chirpFFTPlan->iDist     = chirpPlan->oDist;
        chirpFFTPlan->outStride = chirpFFTPlan->inStride;
        chirpFFTPlan->oDist     = chirpFFTPlan->iDist;
        chirpFFTPlan->AssignParams();

        singlePlan->inStride  = inStride;
        singlePlan->iDist     = iDist;
        singlePlan->outStride = outStride;
        singlePlan->oDist     = oDist;
        singlePlan->AssignParams();
    }
    else if(childNodes.size() == 6)
    {
        auto& chirpPlan  = childNodes[0];
        auto& padmulPlan = childNodes[1];
        auto& ffticPlan  = childNodes[2];
        auto& fftmulPlan = childNodes[3];
        auto& fftrPlan   = childNodes[4];
        auto& resmulPlan = childNodes[5];

        chirpPlan->inStride.push_back(1);
        chirpPlan->iDist = chirpPlan->lengthBlue;
        chirpPlan->outStride.push_back(1);
        chirpPlan->oDist = chirpPlan->lengthBlue;

        padmulPlan->inStride = inStride;
        padmulPlan->iDist    = iDist;

        padmulPlan->outStride.push_back(1);
        padmulPlan->oDist = padmulPlan->lengthBlue;
        for(size_t index = 1; index < length.size(); index++)
        {
            padmulPlan->outStride.push_back(padmulPlan->oDist);
            padmulPlan->oDist *= length[index];
        }

        ffticPlan->inStride  = chirpPlan->outStride;
        ffticPlan->iDist     = chirpPlan->oDist;
        ffticPlan->outStride = ffticPlan->inStride;
        ffticPlan->oDist     = ffticPlan->iDist;

        ffticPlan->AssignParams();

        fftmulPlan->inStride  = padmulPlan->outStride;
        fftmulPlan->iDist     = padmulPlan->oDist;
        fftmulPlan->outStride = fftmulPlan->inStride;
        fftmulPlan->oDist     = fftmulPlan->iDist;

        fftrPlan->inStride  = fftmulPlan->outStride;
        fftrPlan->iDist     = fftmulPlan->oDist;
        fftrPlan->outStride = fftrPlan->inStride;
        fftrPlan->oDist     = fftrPlan->iDist;

        fftrPlan->AssignParams();

        resmulPlan->inStride  = fftrPlan->outStride;
        resmulPlan->iDist     = fftrPlan->oDist;
        resmulPlan->outStride = outStride;
        resmulPlan->oDist     = oDist;
    }
    else
    {
        throw std::runtime_error("unexpected bluestein plan shape");
    }
}

BluesteinSingleNode::BluesteinSingleNode(TreeNode* p, ComputeScheme s)
    : LeafNode(p, s)
{
    need_twd_table = true;
}

bool BluesteinSingleNode::SizeFits(size_t length, rocfft_precision precision)
{
    // 2N - 1 must fit into a single kernel
    return 2 * length - 1 < function_pool::get_largest_length(precision);
}

size_t BluesteinSingleNode::GetTwiddleTableLength()
{
    // FFT part of bluestein needs twiddles
    return lengthBlue;
}

void BluesteinSingleNode::GetKernelFactors()
{
    // HACK: for single-kernel bluestein, avoid radix-16 as it uses a
    // lot of VGPRs.  these kernels already do a lot of other stuff
    // besides FFTs, so we need to keep VGPR usage down to get enough
    // occupancy.  fortunately, single-kernel bluestein is always
    // using pow2 <= 4096, and only at length 2048 do we start to
    // want radix-16 anyway.
    if(lengthBlue == 2048)
        kernelFactors = {8, 8, 8, 4};
    else if(lengthBlue == 4096)
        kernelFactors = {8, 8, 8, 8};
    else
        kernelFactors
            = function_pool::get_kernel(fpkey(lengthBlue, precision, CS_KERNEL_STOCKHAM)).factors;
}
