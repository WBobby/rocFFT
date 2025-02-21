// Copyright (C) 2022 Advanced Micro Devices, Inc. All rights reserved.
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

import graph;
import stats;

size(300, 200, IgnoreAspect);

scale(Linear, Linear);

string filename;

int nbinmult = 0;

usersetting();

if(filename == "") {
  filename = getstring("histogram data file");
}

file fin = input(filename).line();
real[] a = fin;


int N = bins(a);

histogram(a, min(0,min(a)), max(0, max(a)), N, normalize=false, low=0, lightred, black, bars=true);

xequals(0.0);

//label((min(a), 0), string(min(a), 3), 1.5S);
//label((max(a), 0), string(max(a), 3), 1.5S);

xaxis("Speedup \%", BottomTop, LeftTicks);
yaxis("Number of Transforms", LeftRight, RightTicks(trailingzero));


//add(legend(),point(E),20E);
