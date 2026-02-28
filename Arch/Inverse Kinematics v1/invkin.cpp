#include <algorithm>
#include <math.h>
#include <iostream>
float a = 127;
float b = 123;
float c = 167;
float k = 0.5;

float kineq(float x, float d) {		//funkcja, która musi równa zero, żeby ramie było wyciągnięte na odległość d
	return -a*a*(1-k)*(1-x) + -b*b*(k*k*k-k*k+x*k-x*k*k) + c*c*x*k - d*d*(x*x*x - x*x + x*k - x*x*k);
}

float kineqder(float x, float d) {	//pochodna funkcji wyżej
	return -a*a*(k-1)+b*b*(k-1)*k + c*c*k + d*d*(k*(2*x-1)+(2-3*x)*x);
}

float newton(float x, float d, int n) {	//przybliżanie miejsca zerowego funkcji metodą newtona
	for(int i = 0; i < n;i++) x = x - (kineq(x,d)/kineqder(x,d));
	return x;
}

struct armpos{		//jakieś ustawenie ramienia
	float ang1;
	float ang2;
	float ang3;
};

armpos getangles(float x,float d){	//obliczanie wartości kątów przy drugim i trzecim serwie aby ramie bylo wyciągnięte na odległość d
	armpos res;
	res.ang1 = 0;	//kąt przy pierwszym serwie jest determinowany w innej funkcji

	res.ang2 = a*a + k*k*b*b - x*x*d*d;			//tw. cos 
	res.ang3 = (1-k)*(1-k)*b*b + c*c - (1-x)*(1-x)*d*d;	//tw. cos	

        res.ang2 /= 2*k*a*b;					
        res.ang3 /= 2*(1-k)*b*c;				

        res.ang2 = acos(res.ang2) * 57.2958;			//radiany na stopnie
        res.ang3 = acos(res.ang3) * 57.2958;
        return res;
}

armpos pointangles(float x0, float y0){
	float d = sqrt(x0*x0+y0*y0);	//odległość [0,0] od [x0,y0]
	float l = a + b + c;		//długość ramienia
	float x = newton(0.5, d, 3);	//rozwiązanie równania wyżej

	k = 0.5 - std::min((y0-l/2)/l, (x0-l/2)/l);	//constraint który ma zagwarantować, że ramie nie uderzy w podłoże (to pewnie da się zrobić lepiej)
	
	armpos res = getangles(x,d);
	res.ang1 = x*x*d*d + a*a - k*k*b*b;	//tw. cos
	res.ang1 /= 2*a*x*d;
	res.ang1 = asin(y0/d)+acos(res.ang1);	//uwzględnianie [x0,y0]
	res.ang1 *= 57.2958;			//radiany na stopnie

	return res;
}

int main(){
	armpos test = pointangles(200,200);
	std::cout << test.ang1 << std::endl;
	std::cout << test.ang2 << std::endl;
	std::cout << test.ang3 << std::endl;
	return 0;
}

