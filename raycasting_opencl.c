#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "raycasting_opencl.h"
#include "texture/All_Textures.ppm"

float player_x, player_y, player_DeltaX, player_DeltaY, angle;
float frame1 = 0.0f, frame2 = 0.0f, fps = 16.0f;
ButtonKeys Keys = {0, 0, 0, 0};
unsigned char frameBuffer[SCREEN_W * SCREEN_H * 3];

int mapX = 8;
int mapY = 8;
int mapSum = 64;

int mapWall[] = {
    1,1,1,1,1,1,1,1,
    1,0,0,0,1,0,0,1,
    1,3,1,1,1,0,0,1,
    1,0,0,0,1,3,1,1,
    1,0,0,0,0,0,0,1,
    1,0,0,0,0,0,0,1,
    1,0,0,0,0,0,0,1,
    1,1,1,1,1,1,1,1,
};

int mapFloor[] = {
    1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,
};

int mapCeiling[] = {
    3,3,3,3,3,3,3,3,
    3,3,3,3,3,3,3,3,
    3,3,0,0,0,0,3,3,
    3,3,0,0,0,0,3,3,
    3,3,0,0,0,0,3,3,
    3,3,3,3,3,3,3,3,
    3,3,3,3,3,3,3,3,
    3,3,3,3,3,3,3,3,
};

static cl_platform_id g_platform = NULL;
static cl_device_id g_device = NULL;
static cl_context g_context = NULL;
static cl_command_queue g_queue = NULL;
static cl_program g_program = NULL;
static cl_kernel g_kernel = NULL;
static cl_mem g_mapWallBuf = NULL;
static cl_mem g_textureBuf = NULL;
static cl_mem g_outputBuf = NULL;
static cl_mem g_paramsBuf = NULL;

static const size_t TEXTURE_BYTES = sizeof(All_Textures);

static const char *kernelSource =
"typedef struct {\n"
"    float player_x;\n"
"    float player_y;\n"
"    float player_DeltaX;\n"
"    float player_DeltaY;\n"
"    float angle;\n"
"    int mapX;\n"
"    int mapY;\n"
"    int mapSum;\n"
"    int screenW;\n"
"    int screenH;\n"
"    int rayCount;\n"
"} KernelParams;\n"
"\n"
"float degtorad(float a) { return a * 3.1415926535f / 180.0f; }\n"
"float FixAng(float a) {\n"
"    if (a > 359.0f) a -= 360.0f;\n"
"    if (a < 0.0f) a += 360.0f;\n"
"    return a;\n"
"}\n"
"\n"
"__kernel void render_columns(__global const int* mapWall,\n"
"                             __global const uchar* textures,\n"
"                             __global uchar* output,\n"
"                             __constant KernelParams* params) {\n"
"    int r = get_global_id(0);\n"
"    if (r >= params->rayCount) return;\n"
"\n"
"    int mapCoordinateX, mapCoordinateY, mapIndex, dof;\n"
"    float verticalX, verticalY, rayX, rayY, xo, yo;\n"
"    float disVertical, disHorizontal;\n"
"    int vmt = 0, hmt = 0;\n"
"\n"
"    float ra = FixAng(params->angle + 30.0f - ((float)r * 60.0f / (float)params->rayCount));\n"
"\n"
"    dof = 0;\n"
"    disVertical = 100000.0f;\n"
"    float Tan = tan(degtorad(ra));\n"
"\n"
"    if (cos(degtorad(ra)) > 0.001f) {\n"
"        rayX = (((int)params->player_x >> 6) << 6) + 64.0f;\n"
"        rayY = (params->player_x - rayX) * Tan + params->player_y;\n"
"        xo = 64.0f;\n"
"        yo = -xo * Tan;\n"
"    } else if (cos(degtorad(ra)) < -0.001f) {\n"
"        rayX = (((int)params->player_x >> 6) << 6) - 0.0001f;\n"
"        rayY = (params->player_x - rayX) * Tan + params->player_y;\n"
"        xo = -64.0f;\n"
"        yo = -xo * Tan;\n"
"    } else {\n"
"        rayX = params->player_x;\n"
"        rayY = params->player_y;\n"
"        dof = 8;\n"
"    }\n"
"\n"
"    while (dof < 8) {\n"
"        mapCoordinateX = ((int)rayX) >> 6;\n"
"        mapCoordinateY = ((int)rayY) >> 6;\n"
"        mapIndex = mapCoordinateY * params->mapX + mapCoordinateX;\n"
"\n"
"        if (mapIndex > 0 && mapIndex < params->mapX * params->mapY && mapWall[mapIndex] > 0) {\n"
"            vmt = mapWall[mapIndex] - 1;\n"
"            dof = 8;\n"
"            disVertical = cos(degtorad(ra)) * (rayX - params->player_x) - sin(degtorad(ra)) * (rayY - params->player_y);\n"
"        } else {\n"
"            rayX += xo;\n"
"            rayY += yo;\n"
"            dof += 1;\n"
"        }\n"
"    }\n"
"\n"
"    verticalX = rayX;\n"
"    verticalY = rayY;\n"
"\n"
"    dof = 0;\n"
"    disHorizontal = 100000.0f;\n"
"    if (fabs(Tan) < 0.000001f) Tan = 0.000001f;\n"
"    Tan = 1.0f / Tan;\n"
"\n"
"    if (sin(degtorad(ra)) > 0.001f) {\n"
"        rayY = (((int)params->player_y >> 6) << 6) - 0.0001f;\n"
"        rayX = (params->player_y - rayY) * Tan + params->player_x;\n"
"        yo = -64.0f;\n"
"        xo = -yo * Tan;\n"
"    } else if (sin(degtorad(ra)) < -0.001f) {\n"
"        rayY = (((int)params->player_y >> 6) << 6) + 64.0f;\n"
"        rayX = (params->player_y - rayY) * Tan + params->player_x;\n"
"        yo = 64.0f;\n"
"        xo = -yo * Tan;\n"
"    } else {\n"
"        rayX = params->player_x;\n"
"        rayY = params->player_y;\n"
"        dof = 8;\n"
"    }\n"
"\n"
"    while (dof < 8) {\n"
"        mapCoordinateX = ((int)rayX) >> 6;\n"
"        mapCoordinateY = ((int)rayY) >> 6;\n"
"        mapIndex = mapCoordinateY * params->mapX + mapCoordinateX;\n"
"\n"
"        if (mapIndex > 0 && mapIndex < params->mapX * params->mapY && mapWall[mapIndex] > 0) {\n"
"            hmt = mapWall[mapIndex] - 1;\n"
"            dof = 8;\n"
"            disHorizontal = cos(degtorad(ra)) * (rayX - params->player_x) - sin(degtorad(ra)) * (rayY - params->player_y);\n"
"        } else {\n"
"            rayX += xo;\n"
"            rayY += yo;\n"
"            dof += 1;\n"
"        }\n"
"    }\n"
"\n"
"    float shade = 1.0f;\n"
"    if (disVertical < disHorizontal) {\n"
"        hmt = vmt;\n"
"        shade = 0.5f;\n"
"        rayX = verticalX;\n"
"        rayY = verticalY;\n"
"        disHorizontal = disVertical;\n"
"    }\n"
"\n"
"    int ca = (int)FixAng(params->angle - ra);\n"
"    disHorizontal = disHorizontal * cos(degtorad((float)ca));\n"
"    if (disHorizontal < 0.0001f) disHorizontal = 0.0001f;\n"
"\n"
"    int lineH = (int)((params->mapSum * params->screenH) / disHorizontal);\n"
"    float ty_step = 32.0f / (float)(lineH > 0 ? lineH : 1);\n"
"    float ty_off = 0.0f;\n"
"\n"
"    if (lineH > params->screenH) {\n"
"        ty_off = (float)(lineH - params->screenH) / 2.0f;\n"
"        lineH = params->screenH;\n"
"    }\n"
"\n"
"    int lineOff = (params->screenH / 2) - (lineH >> 1);\n"
"\n"
"    float tx;\n"
"    if (shade == 1.0f) {\n"
"        tx = fmod(floor(rayX / 2.0f), 32.0f);\n"
"        if (ra > 180.0f) tx = 31.0f - tx;\n"
"    } else {\n"
"        tx = fmod(floor(rayY / 2.0f), 32.0f);\n"
"        if (ra > 90.0f && ra < 270.0f) tx = 31.0f - tx;\n"
"    }\n"
"\n"
"    if (tx < 0.0f) tx += 32.0f;\n"
"    int texX = (int)tx;\n"
"    if (texX < 0) texX = 0;\n"
"    if (texX > 31) texX = 31;\n"
"\n"
"    int xStart = r * params->screenW / params->rayCount;\n"
"    int xEnd = (r + 1) * params->screenW / params->rayCount;\n"
"\n"
"    for (int y = 0; y < params->screenH; ++y) {\n"
"        uchar red = 60, green = 60, blue = 60;\n"
"\n"
"        if (y < lineOff) {\n"
"            red = 100;\n"
"            green = 160;\n"
"            blue = 220;\n"
"        } else if (y >= lineOff && y < lineOff + lineH) {\n"
"            float ty = ty_off * ty_step + (float)(y - lineOff) * ty_step;\n"
"            int texY = (int)ty;\n"
"            if (texY < 0) texY = 0;\n"
"            if (texY > 31) texY = 31;\n"
"\n"
"            int pixel = ((texY * 32) + texX) * 3 + (hmt * 32 * 32 * 3);\n"
"            red   = (uchar)((float)textures[pixel + 0] * shade);\n"
"            green = (uchar)((float)textures[pixel + 1] * shade);\n"
"            blue  = (uchar)((float)textures[pixel + 2] * shade);\n"
"        }\n"
"\n"
"        for (int x = xStart; x < xEnd; ++x) {\n"
"            int out = (y * params->screenW + x) * 3;\n"
"            output[out + 0] = red;\n"
"            output[out + 1] = green;\n"
"            output[out + 2] = blue;\n"
"        }\n"
"    }\n"
"}\n";

static void check_cl(cl_int err, const char *what)
{
    if (err != CL_SUCCESS) {
        fprintf(stderr, "OpenCL hiba (%s): %d\n", what, err);
        exit(EXIT_FAILURE);
    }
}

float degtorad(float a)
{
    return a * PI / 180.0f;
}

float FixAng(float a)
{
    if (a > 359.0f) a -= 360.0f;
    if (a < 0.0f) a += 360.0f;
    return a;
}

void init(void)
{
    glClearColor(0.3f, 0.3f, 0.3f, 1.0f);
    gluOrtho2D(0, SCREEN_W, SCREEN_H, 0);

    player_x = 300.0f;
    player_y = 300.0f;
    angle = 0.0f;
    player_DeltaX = cosf(degtorad(angle));
    player_DeltaY = -sinf(degtorad(angle));

    memset(frameBuffer, 0, sizeof(frameBuffer));
}

void init_opencl(void)
{
    cl_int err;
    cl_uint platformCount = 0;
    cl_uint deviceCount = 0;

    check_cl(clGetPlatformIDs(1, &g_platform, &platformCount), "clGetPlatformIDs");
    if (platformCount == 0) {
        fprintf(stderr, "Nem talalhato OpenCL platform.\n");
        exit(EXIT_FAILURE);
    }

    err = clGetDeviceIDs(g_platform, CL_DEVICE_TYPE_GPU, 1, &g_device, &deviceCount);
    if (err != CL_SUCCESS || deviceCount == 0) {
        err = clGetDeviceIDs(g_platform, CL_DEVICE_TYPE_CPU, 1, &g_device, &deviceCount);
        check_cl(err, "clGetDeviceIDs CPU fallback");
    }

    g_context = clCreateContext(NULL, 1, &g_device, NULL, NULL, &err);
    check_cl(err, "clCreateContext");

#if CL_TARGET_OPENCL_VERSION >= 200
    g_queue = clCreateCommandQueueWithProperties(g_context, g_device, 0, &err);
#else
    g_queue = clCreateCommandQueue(g_context, g_device, 0, &err);
#endif
    check_cl(err, "clCreateCommandQueue");

    g_program = clCreateProgramWithSource(g_context, 1, &kernelSource, NULL, &err);
    check_cl(err, "clCreateProgramWithSource");

    err = clBuildProgram(g_program, 1, &g_device, NULL, NULL, NULL);
    if (err != CL_SUCCESS) {
        size_t logSize = 0;
        clGetProgramBuildInfo(g_program, g_device, CL_PROGRAM_BUILD_LOG, 0, NULL, &logSize);

        char *log = (char*)malloc(logSize + 1);
        if (log) {
            clGetProgramBuildInfo(g_program, g_device, CL_PROGRAM_BUILD_LOG, logSize, log, NULL);
            log[logSize] = '\0';
            fprintf(stderr, "Build log:\n%s\n", log);
            free(log);
        }

        check_cl(err, "clBuildProgram");
    }

    g_kernel = clCreateKernel(g_program, "render_columns", &err);
    check_cl(err, "clCreateKernel");

    g_mapWallBuf = clCreateBuffer(
        g_context,
        CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        sizeof(mapWall),
        mapWall,
        &err
    );
    check_cl(err, "clCreateBuffer mapWall");

    g_textureBuf = clCreateBuffer(
        g_context,
        CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        TEXTURE_BYTES,
        (void*)All_Textures,
        &err
    );
    check_cl(err, "clCreateBuffer textures");

    g_outputBuf = clCreateBuffer(
        g_context,
        CL_MEM_WRITE_ONLY,
        sizeof(frameBuffer),
        NULL,
        &err
    );
    check_cl(err, "clCreateBuffer output");

    g_paramsBuf = clCreateBuffer(
        g_context,
        CL_MEM_READ_ONLY,
        sizeof(KernelParams),
        NULL,
        &err
    );
    check_cl(err, "clCreateBuffer params");
}

void cleanup_opencl(void)
{
    if (g_paramsBuf) clReleaseMemObject(g_paramsBuf);
    if (g_outputBuf) clReleaseMemObject(g_outputBuf);
    if (g_textureBuf) clReleaseMemObject(g_textureBuf);
    if (g_mapWallBuf) clReleaseMemObject(g_mapWallBuf);
    if (g_kernel) clReleaseKernel(g_kernel);
    if (g_program) clReleaseProgram(g_program);
    if (g_queue) clReleaseCommandQueue(g_queue);
    if (g_context) clReleaseContext(g_context);
}

void computeFrameOpenCL(void)
{
    size_t globalSize = RAY_COUNT;
    KernelParams params;

    params.player_x = player_x;
    params.player_y = player_y;
    params.player_DeltaX = player_DeltaX;
    params.player_DeltaY = player_DeltaY;
    params.angle = angle;
    params.mapX = mapX;
    params.mapY = mapY;
    params.mapSum = mapSum;
    params.screenW = SCREEN_W;
    params.screenH = SCREEN_H;
    params.rayCount = RAY_COUNT;

    check_cl(
        clEnqueueWriteBuffer(g_queue, g_paramsBuf, CL_TRUE, 0, sizeof(params), &params, 0, NULL, NULL),
        "clEnqueueWriteBuffer params"
    );

    check_cl(clSetKernelArg(g_kernel, 0, sizeof(cl_mem), &g_mapWallBuf), "clSetKernelArg 0");
    check_cl(clSetKernelArg(g_kernel, 1, sizeof(cl_mem), &g_textureBuf), "clSetKernelArg 1");
    check_cl(clSetKernelArg(g_kernel, 2, sizeof(cl_mem), &g_outputBuf), "clSetKernelArg 2");
    check_cl(clSetKernelArg(g_kernel, 3, sizeof(cl_mem), &g_paramsBuf), "clSetKernelArg 3");

    check_cl(
        clEnqueueNDRangeKernel(g_queue, g_kernel, 1, NULL, &globalSize, NULL, 0, NULL, NULL),
        "clEnqueueNDRangeKernel"
    );
    check_cl(clFinish(g_queue), "clFinish");

    check_cl(
        clEnqueueReadBuffer(g_queue, g_outputBuf, CL_TRUE, 0, sizeof(frameBuffer), frameBuffer, 0, NULL, NULL),
        "clEnqueueReadBuffer output"
    );
}

void drawFrameBuffer(void)
{
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, SCREEN_W, 0, SCREEN_H, -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glRasterPos2i(0, SCREEN_H - 1);
    glPixelZoom(1.0f, -1.0f);

    glDrawPixels(SCREEN_W, SCREEN_H, GL_RGB, GL_UNSIGNED_BYTE, frameBuffer);

    glPixelZoom(1.0f, 1.0f);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();

    glMatrixMode(GL_MODELVIEW);
}

void drawplayer(void)
{
    float scale = 32.0f / 64.0f;

    glColor3f(1.0f, 1.0f, 0.0f);
    glPointSize(4.0f);
    glBegin(GL_POINTS);
    glVertex2f(player_x * scale, player_y * scale);
    glEnd();

    glLineWidth(2.0f);
    glBegin(GL_LINES);
    glVertex2f(player_x * scale, player_y * scale);
    glVertex2f((player_x + player_DeltaX * 20.0f) * scale,
               (player_y + player_DeltaY * 20.0f) * scale);
    glEnd();
}

void drawMap2D(void)
{
    int tileSize = 32;

    for (int y = 0; y < mapY; ++y) {
        for (int x = 0; x < mapX; ++x) {
            if (mapWall[y * mapX + x] > 0) {
                glColor3f(1.0f, 1.0f, 1.0f);
            } else {
                glColor3f(0.0f, 0.0f, 0.0f);
            }

            int x0 = x * tileSize;
            int y0 = y * tileSize;

            glBegin(GL_QUADS);
            glVertex2i(x0, y0);
            glVertex2i(x0, y0 + tileSize);
            glVertex2i(x0 + tileSize, y0 + tileSize);
            glVertex2i(x0 + tileSize, y0);
            glEnd();
        }
    }
}

void display(void)
{
    frame2 = glutGet(GLUT_ELAPSED_TIME);
    fps = frame2 - frame1;
    if (fps < 1.0f) fps = 1.0f;
    frame1 = frame2;

    int x0 = (player_DeltaX < 0.0f) ? -20 : 20;
    int y0 = (player_DeltaY < 0.0f) ? -20 : 20;

    int ipx = (int)(player_x / 64.0f);
    int ipy = (int)(player_y / 64.0f);
    int ipx_add_x0 = (int)((player_x + x0) / 64.0f);
    int ipx_sub_x0 = (int)((player_x - x0) / 64.0f);
    int ipy_add_y0 = (int)((player_y + y0) / 64.0f);
    int ipy_sub_y0 = (int)((player_y - y0) / 64.0f);

    if (Keys.w == 1) {
        if (mapWall[ipy * mapX + ipx_add_x0] == 0) player_x += player_DeltaX * 0.2f * fps;
        if (mapWall[ipy_add_y0 * mapX + ipx] == 0) player_y += player_DeltaY * 0.2f * fps;
    }

    if (Keys.s == 1) {
        if (mapWall[ipy * mapX + ipx_sub_x0] == 0) player_x -= player_DeltaX * 0.2f * fps;
        if (mapWall[ipy_sub_y0 * mapX + ipx] == 0) player_y -= player_DeltaY * 0.2f * fps;
    }

    if (Keys.a == 1) {
        angle = FixAng(angle + 0.2f * fps);
        player_DeltaX = cosf(degtorad(angle));
        player_DeltaY = -sinf(degtorad(angle));
    }

    if (Keys.d == 1) {
        angle = FixAng(angle - 0.2f * fps);
        player_DeltaX = cosf(degtorad(angle));
        player_DeltaY = -sinf(degtorad(angle));
    }

    computeFrameOpenCL();

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawFrameBuffer();
    drawMap2D();
    drawplayer();
    glutSwapBuffers();
    glutPostRedisplay();
}

void ButtonDown(unsigned char key, int x, int y)
{
    (void)x;
    (void)y;

    if (key == 'a') Keys.a = 1;
    if (key == 'd') Keys.d = 1;
    if (key == 'w') Keys.w = 1;
    if (key == 's') Keys.s = 1;

    if (key == 'e') {
        int x0 = (player_DeltaX < 0.0f) ? -25 : 25;
        int y0 = (player_DeltaY < 0.0f) ? -25 : 25;
        int ipx_add_x0 = (int)((player_x + x0) / 64.0f);
        int ipy_add_y0 = (int)((player_y + y0) / 64.0f);

        if (mapWall[ipy_add_y0 * mapX + ipx_add_x0] == 3) {
            mapWall[ipy_add_y0 * mapX + ipx_add_x0] = 0;
            clEnqueueWriteBuffer(g_queue, g_mapWallBuf, CL_TRUE, 0, sizeof(mapWall), mapWall, 0, NULL, NULL);
        }
    }
}

void ButtonUp(unsigned char key, int x, int y)
{
    (void)x;
    (void)y;

    if (key == 'a') Keys.a = 0;
    if (key == 'd') Keys.d = 0;
    if (key == 'w') Keys.w = 0;
    if (key == 's') Keys.s = 0;
}

void resize(int w, int h)
{
    (void)w;
    (void)h;
    glutReshapeWindow(SCREEN_W, SCREEN_H);
}