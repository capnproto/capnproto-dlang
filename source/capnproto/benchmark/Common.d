// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
// Licensed under the MIT License:
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
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

module capnproto.benchmark.Common;

class FastRand
{
public: //Methods.
	int nextInt()
	{
		int tmp = this.x ^ (this.x << 11);
		this.x = this.y;
		this.y = this.z;
		this.z = this.w;
		this.w = this.w ^ (this.w >> 19) ^ tmp ^ (tmp >> 8);
		return this.w;
	}
	
	int nextLessThan(int range)
	{
		//Just chop off the sign bit.
		return (0x7fffffff & this.nextInt()) % range;
	}
	
	double nextDouble(double range)
	{
		//Just chop off the sign bit.
		return cast(double)(0x7fffffff & this.nextInt()) * range / cast(double)0x7fffffff;
	}

private: //Variables.
	int x = 0x1d2acd47;
	int y = 0x58ca3e14;
	int z = 0xf563f232;
	int w = 0x0bc76199;
}

int div(int a, int b)
{
	if(b == 0)
		return 0x7fffffff;
	if(a == 0x80000000 && b == -1)
		return 0x7fffffff;
	return a / b;
}

int modulus(int a, int b)
{
	if(b == 0)
		return 0x7fffffff;
	if(a == 0x80000000 && b == -1)
		return 0x7fffffff;
	return a % b;
}


string[] WORDS = [ "foo ", "bar ", "baz ", "qux ", "quux ", "corge ", "grault ", "garply ", "waldo ", "fred ", "plugh ", "xyzzy ", "thud " ];
