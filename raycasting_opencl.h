#ifndef RAYCASTING_OPENCL_H
#define RAYCASTING_OPENCL_H

#define CL_TARGET_OPENCL_VERSION 120

#include <CL/cl.h>
#include <GL/freeglut.h>
#include <stdio.h>

#define SCREEN_W 1024
#define SCREEN_H 576
#define RAY_COUNT 240
#define PI 3.1415926535f

extern float player_x, player_y, player_DeltaX, player_DeltaY, angle;
extern int mapWall[], mapFloor[], mapCeiling[];
extern int mapX, mapY, mapSum;
extern float frame1, frame2, fps;
extern unsigned char frameBuffer[SCREEN_W * SCREEN_H * 3];

typedef struct
{
    int w, a, s, d;
} ButtonKeys;

typedef struct
{
    float player_x;
    float player_y;
    float player_DeltaX;
    float player_DeltaY;
    float angle;
    int mapX;
    int mapY;
    int mapSum;
    int screenW;
    int screenH;
    int rayCount;
} KernelParams;

extern ButtonKeys Keys;

float degtorad(float a);
float FixAng(float a);

void init(void);
void init_opencl(void);
void cleanup_opencl(void);
void computeFrameOpenCL(void);
void drawFrameBuffer(void);
void drawplayer(void);
void drawMap2D(void);
void display(void);
void ButtonDown(unsigned char key, int x, int y);
void ButtonUp(unsigned char key, int x, int y);
void resize(int w, int h);

#endif