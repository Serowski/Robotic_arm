import math
def kineq(x,d):
    global a,b,c,k
    return -(a**2)*(1-k)*(1-x) + -(b**2)*(k**3-k**2+x*k-x*(k**2)) + (c**2)*x*k - (d**2)*(x**3 - x**2 + x*k - (x**2)*k)

def kineqder(x,d):
    global a,b,c,k
    return -(a**2)*(k-1)+(b**2)*(k-1)*k + (c**2)*k + (d**2)*(k*(2*x-1)+(2-3*x)*x)

def newton(x,d,n):
    for i in range(n):
        x = x - (kineq(x,d)/kineqder(x,d))
    return x

def getangles(x,d):
    global a,b,c,k
    ang2 = a**2 + (k**2)*(b**2) - (x**2) * (d**2)
    ang3 = ((1-k)**2) * (b**2) + (c**2) - ((1-x)**2)*(d**2)
    ang2 /= 2*k*a*b
    ang3 /= 2*(1-k)*b*c

    if ang2 > 1 or ang2 < -1 or ang3 > 1 or ang3 < -1:
        return None

    ang2 = math.acos(ang2) * 57.2958 
    ang3 = math.acos(ang3) * 57.2958
    
    return ang2, ang3

def pointangles(x0,y0):
    global k,a,b,c
    d = math.sqrt(x0*x0+y0*y0)
    l = a + b + c
    if d > l:
        return None

    k = 0.5 - min((y0-l/2)/l, (x0-l/2)/l)
    #k = 0.5 - min((y0-250)/500, (x0-250)/500)
    
    x = newton(0.5,d,3) 
    ang1 = (x**2)*(d**2)+(a**2) - (k**2)*(b**2)
    ang1 /= 2*a*x*d
    
    if ang1 > 1 or ang1 < -1 or x < 0 or x > 1:
        return None

    ang1 = math.asin(y0 / d) + math.acos(ang1)
    ang1 *= 57.2958
    ang = getangles(x,d)
    if getangles(x,d) != None:
        ang2, ang3 = ang
    else:
        return None

    return ang1, ang2, ang3

a = 127
b = 123
c = 167
k = 0.5

print(pointangles(200,100))
