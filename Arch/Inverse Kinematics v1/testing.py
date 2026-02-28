import pygame,sys,math
import invkin as kin
screen = pygame.display.set_mode((500,500))
screen.fill((0,0,0))
lcolor = (255,255,255)

def drawarm(a,b,c,alpha,beta,gamma):
    screen.fill((0,0,0))
    v = pygame.math.Vector2(a,0).rotate(-alpha)
    x,y = 0,500
    pygame.draw.line(screen, lcolor, (x,y), (x+v.x, y+v.y))
    x += v.x
    y += v.y
    v = (v.normalize() * b).rotate(180 - beta)
    pygame.draw.line(screen, lcolor, (x,y), (x+v.x, y+v.y))
    x+=v.x
    y+=v.y
    v = (v.normalize() * c).rotate( gamma+180)
    pygame.draw.line(screen, lcolor, (x,y), (x+v.x, y+v.y))
    pygame.display.flip()

a = 127
b = 123
c = 167
kin.a = a
kin.b = b
kin.c = c
while True:
    x,y = pygame.mouse.get_pos()
    y = 500 - y
    ang = None
    if x > 10 and y > 10 and x < 490 and y < 490:
        ang = kin.pointangles(x,y)
        print(ang)
    if ang != None:
        alpha,beta,gamma = ang
        drawarm(a,b,c, alpha, beta, gamma)
    for events in pygame.event.get():
        if events.type == pygame.QUIT:
            sys.exit(0)
