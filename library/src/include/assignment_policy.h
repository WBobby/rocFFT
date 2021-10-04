// Copyright (c) 2021 - present Advanced Micro Devices, Inc. All rights reserved.
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

#ifndef ASSIGNMENT_POLICY_H
#define ASSIGNMENT_POLICY_H

#include "tree_node.h"
#include <vector>

/****************************************************************************
 * A expanding tree recording all the legal assignments
 * For each path, from root to each leaf-node, we store information
 * of the assignment in this struct, (curr-Node, i/oBuf, i/oAryType, IP/OP)
 * and also update some accumulated values which will be propagated to leaves.
 * (total numbuer of IP, FusedNodes, ArrayTypeSwitching...)
 * NOTE:
 *   the tree is not a complete tree, since we have lots of tests that do early
 *   rejection which can stop growing the branches
 ****************************************************************************/
struct PlacementTrace
{
    TreeNode*         curNode          = nullptr;
    OperatingBuffer   inBuf            = OB_UNINIT;
    OperatingBuffer   outBuf           = OB_UNINIT;
    bool              isInplace        = false;
    rocfft_array_type iType            = rocfft_array_type_unset;
    rocfft_array_type oType            = rocfft_array_type_unset;
    size_t            numInplace       = 0;
    size_t            placementScore   = 0;
    size_t            numTypeSwitching = 0;
    size_t            numFusedNodes    = 0;

    // parent for back-tracking, placement branches
    PlacementTrace*                              parent = nullptr;
    std::vector<std::unique_ptr<PlacementTrace>> branches;
    std::set<OperatingBuffer>                    usedBuffers;

    PlacementTrace() {}

    PlacementTrace(TreeNode*         node,
                   OperatingBuffer   iB,
                   OperatingBuffer   oB,
                   rocfft_array_type inType,
                   rocfft_array_type outType,
                   PlacementTrace*   p)
        : curNode(node)
        , inBuf(iB)
        , outBuf(oB)
        , iType(inType)
        , oType(outType)
        , parent(p)
    {
        isInplace        = (iB == oB);
        numInplace       = parent->numInplace + (isInplace ? 1 : 0);
        numTypeSwitching = parent->numTypeSwitching + (inType != outType ? 1 : 0);
        // using set to record the used buffers. (we have 5 buffers at most)
        usedBuffers = parent->usedBuffers;
        usedBuffers.insert(iB);
        usedBuffers.insert(oB);

        // In 3D_RTRT, we found sbcc len 168 performs better in out-of-place,
        // so we do some hack for that. But eventually the best way is to make it
        // 3D_TRTRTR and the FuseShim can beat 3D_RTRT,
        // Leave this comment for now until we've fixed the len 168..
#if 0
        // is the placement preferable for this kernel?
        // FIXME: see comments in isInplacePreferable (especially for sbcc len168),
        //        sbcc len168, OP performs much better than IP, eventually we need fix this
        size_t score = 0;
        if(isInplace == curNode->isInplacePreferable())
            score += (isInplace ? 1 : 2);
        placementScore = parent->placementScore + score;
#endif
    }

    // print the [in->out] for this placement
    void Print(rocfft_ostream& os);

    // Starting from the tail (leaf of each branch) back to the head (root),
    // Calculate how many kernel fusions can be done with this assignment.
    size_t BackwardCalcFusions(ExecPlan& execPlan, int curFuseShimID, PlacementTrace* shimLastNode);

    // How many buffers are used in this assignment
    size_t NumUsedBuffers();

    // Starting from the tail (leaf of each branch) back to the head (root),
    // Fill-in the assignment from the PlacemenTraces to the nodes
    void Backtracking(ExecPlan& execPlan, int planID);
};

class AssignmentPolicy
{
public:
    AssignmentPolicy() = default;

    bool AssignBuffers(ExecPlan& execPlan);

private:
    std::vector<size_t> GetEffectiveNodeOutLen(ExecPlan& execPlan, const TreeNode& node);

    // test if rootArrayType == testArrayType,
    // but could be alias if root is real, and test is CI, or root is HI and test is CI
    bool EquivalentArrayType(rocfft_array_type rootAryType, rocfft_array_type testAryType);

    bool BufferIsUnitStride(ExecPlan& execPlan, OperatingBuffer buf);

    bool ValidOutBuffer(ExecPlan&         execPlan,
                        TreeNode&         node,
                        OperatingBuffer   buffer,
                        rocfft_array_type arrayType);

    bool CheckAssignmentValid(ExecPlan& execPlan);

    void UpdateWinnerFromValidPaths(ExecPlan& execPlan);

    void Enumerate(PlacementTrace*   parent,
                   ExecPlan&         execPlan,
                   size_t            curSeqID,
                   OperatingBuffer   startBuf,
                   rocfft_array_type startType);

    std::vector<PlacementTrace*> winnerCandidates;
    std::set<OperatingBuffer>    availableBuffers;
    std::set<rocfft_array_type>  availableArrayTypes;
    int  numCurWinnerFusions; // -1 means no winner, else = curr winner's #-fusions
    bool mustUseTBuffer = false;
    bool mustUseCBuffer = false;
};

#endif // ASSIGNMENT_POLICY_H
