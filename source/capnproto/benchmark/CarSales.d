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

module capnproto.benchmark.CarSales;

import capnproto.StructList;
import capnproto.Text;

import capnproto.benchmark.carsalesschema;
import capnproto.benchmark.Common;
import capnproto.benchmark.TestCase;

void main(string[] args)
{
	CarSales testCase = new CarSales();
	testCase.execute(args);
}

class CarSales : TestCase!(ParkingLot, TotalValue, long)
{
public:
	override long setupRequest(FastRand rng, ParkingLot.Builder request)
	{
		long result = 0;
		auto cars = request.initCars(rng.nextLessThan(200));
		foreach(i; 0..cars.length)
		{
			Car.Builder car = cars.get(i);
			randomCar(rng, car);
			result += carValue(car.asReader());
		}
		return result;
	}
	
	override void handleRequest(ParkingLot.Reader request, TotalValue.Builder response)
	{
		long result = 0;
		foreach(car; request.getCars())
			result += carValue(car);
		response.setAmount(result);
	}
	
	override bool checkResponse(TotalValue.Reader response, long expected)
	{
		return response.getAmount() == expected;
	}

package:
	static long carValue(Car.Reader car)
	{
		long result = 0;
		result += car.getSeats() * 200;
		result += car.getDoors() * 350;
		
		foreach(wheel; car.getWheels())
		{
			result += cast(long)wheel.getDiameter() * cast(long)wheel.getDiameter();
			result += wheel.getSnowTires()? 100 : 0;
		}
		
		result += cast(long)car.getLength() * cast(long)car.getWidth() * cast(long)car.getHeight() / 50;
		
		Engine.Reader engine = car.getEngine();
		result += cast(long)engine.getHorsepower() * 40;
		if(engine.getUsesElectric())
			result += engine.getUsesGas()? 5000 : 3000;
		
		result += car.getHasPowerWindows()? 100 : 0;
		result += car.getHasPowerSteering()? 200 : 0;
		result += car.getHasCruiseControl()? 400 : 0;
		result += car.getHasNavSystem()? 2000 : 0;
		
		result += cast(long)car.getCupHolders() * 25;
		
		return result;
	}
	
	static Text.Reader[] MAKES = [ Text.Reader("Toyota"), Text.Reader("GM"), Text.Reader("Ford"), Text.Reader("Honda"), Text.Reader("Tesla") ];
	static Text.Reader[] MODELS = [ Text.Reader("Camry"), Text.Reader("Prius"), Text.Reader("Volt"), Text.Reader("Accord"), Text.Reader("Leaf"), Text.Reader("Model S") ];
	
	static void randomCar(FastRand rng, ref Car.Builder car)
	{
		car.setMake(MAKES[rng.nextLessThan(cast(int)MAKES.length)]);
		car.setModel(MODELS[rng.nextLessThan(cast(int)MODELS.length)]);
		
		car.setColor(cast(Color)rng.nextLessThan(cast(ushort)Color.silver + 1));
		car.setSeats(cast(byte)(2 + rng.nextLessThan(6)));
		car.setDoors(cast(byte)(2 + rng.nextLessThan(3)));
		
		foreach(wheel; car.initWheels(4))
		{
			wheel.setDiameter(cast(short)(25 + rng.nextLessThan(15)));
			wheel.setAirPressure(cast(float)(30.0 + rng.nextDouble(20.0)));
			wheel.setSnowTires(rng.nextLessThan(16) == 0);
		}
		
		car.setLength(cast(short)(170 + rng.nextLessThan(150)));
		car.setWidth(cast(short)(48 + rng.nextLessThan(36)));
		car.setHeight(cast(short)(54 + rng.nextLessThan(48)));
		car.setWeight(cast(int)car.getLength() * cast(int)car.getWidth() * cast(int)car.getHeight() / 200);
		
		Engine.Builder engine = car.initEngine();
		engine.setHorsepower(cast(short)(100 * rng.nextLessThan(400)));
		engine.setCylinders(cast(byte)(4  + 2 * rng.nextLessThan(3)));
		engine.setCc(800 + rng.nextLessThan(10000));
		engine.setUsesGas(true);
		engine.setUsesElectric(rng.nextLessThan(2) == 1);
		
		car.setFuelCapacity(cast(float)(10.0 + rng.nextDouble(30.0)));
		car.setFuelLevel(cast(float)(rng.nextDouble(car.getFuelCapacity())));
		car.setHasPowerWindows(rng.nextLessThan(2) == 1);
		car.setHasPowerSteering(rng.nextLessThan(2) == 1);
		car.setHasCruiseControl(rng.nextLessThan(2) == 1);
		car.setCupHolders(cast(byte)rng.nextLessThan(12));
		car.setHasNavSystem(rng.nextLessThan(2) == 1);
	}
}
