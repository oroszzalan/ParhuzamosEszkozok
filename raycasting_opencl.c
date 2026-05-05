#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "raycasting_opencl.h"
#include "texture/All_Textures.ppm"
#include "texture/sky.ppm"

float player_x, player_y, player_DeltaX, player_DeltaY, angle;
float frame1 = 0.0f, frame2 = 0.0f, fps = 16.0f;

/* --- Idomereshez --- */
static unsigned int  g_frame_count    = 0;
static double        g_kernel_ms_sum  = 0.0;   /* GPU kernel idok osszege (ns -> ms) */
static double        g_frame_ms_sum   = 0.0;   /* teljes frame idok osszege */
#define PRINT_EVERY  60                         /* hanyadik frame-nel irjon ki */

ButtonKeys Keys = {0, 0, 0, 0};

unsigned char frameBuffer[SCREEN_W * SCREEN_H * 3];

int mapX   = 8;
int mapY   = 8;
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

/* --- OpenCL globalis valtozok --- */
static cl_platform_id   g_platform   = NULL;
static cl_device_id     g_device     = NULL;
static cl_context       g_context    = NULL;
static cl_command_queue g_queue      = NULL;
static cl_program       g_program    = NULL;
static cl_kernel        g_kernel     = NULL;
static cl_mem           g_mapWallBuf    = NULL;
static cl_mem           g_mapFloorBuf   = NULL;
static cl_mem           g_mapCeilingBuf = NULL;
static cl_mem           g_textureBuf    = NULL;
static cl_mem           g_skyBuf        = NULL;
static cl_mem           g_outputBuf  = NULL;
static cl_mem           g_paramsBuf  = NULL;

static const size_t TEXTURE_BYTES = sizeof(All_Textures);
static const size_t SKY_BYTES     = sizeof(sky);

/* -----------------------------------------------------------------------
 * load_kernel_source
 * Beolvassa a .cl fajl tartalmat egy dinamikusan foglalt bufferbe.
 * A hivónak kell free()-lni a visszakapott pointert.
 * ----------------------------------------------------------------------- */
static char *load_kernel_source(const char *filename, size_t *out_len)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "Nem sikerult megnyitni a kernel fajlt: %s\n", filename);
        exit(EXIT_FAILURE);
    }

    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    rewind(fp);

    char *src = (char *)malloc(len + 1);
    if (!src) {
        fprintf(stderr, "Memoria allokacio sikertelen (kernel forras)\n");
        fclose(fp);
        exit(EXIT_FAILURE);
    }

    size_t nread = fread(src, 1, len, fp);
    fclose(fp);

    src[nread] = '\0';
    if (out_len) *out_len = nread;

    return src;
}

/* ----------------------------------------------------------------------- */

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
    if (a <   0.0f) a += 360.0f;
    return a;
}

void init(void)
{
    glClearColor(0.3f, 0.3f, 0.3f, 1.0f);
    gluOrtho2D(0, SCREEN_W, SCREEN_H, 0);

    player_x      = 300.0f;
    player_y      = 300.0f;
    angle         = 0.0f;
    player_DeltaX = cosf(degtorad(angle));
    player_DeltaY = -sinf(degtorad(angle));

    memset(frameBuffer, 0, sizeof(frameBuffer));
}

void init_opencl(void)
{
    cl_int  err;
    cl_uint platformCount = 0;
    cl_uint deviceCount   = 0;

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

    /* CL_QUEUE_PROFILING_ENABLE kell a clGetEventProfilingInfo-hoz */
#if CL_TARGET_OPENCL_VERSION >= 200
    cl_queue_properties props[] = {
        CL_QUEUE_PROPERTIES, CL_QUEUE_PROFILING_ENABLE, 0
    };
    g_queue = clCreateCommandQueueWithProperties(g_context, g_device, props, &err);
#else
    g_queue = clCreateCommandQueue(g_context, g_device,
                                   CL_QUEUE_PROFILING_ENABLE, &err);
#endif
    check_cl(err, "clCreateCommandQueue (profiling enabled)");

    /* --- Kernel beolvasasa fajlbol --- */
    size_t srcLen    = 0;
    char  *kernelSrc = load_kernel_source("raycasting_kernel.cl", &srcLen);

    g_program = clCreateProgramWithSource(g_context, 1,
                                          (const char **)&kernelSrc,
                                          &srcLen, &err);
    free(kernelSrc);
    check_cl(err, "clCreateProgramWithSource");

    err = clBuildProgram(g_program, 1, &g_device, NULL, NULL, NULL);
    if (err != CL_SUCCESS) {
        size_t logSize = 0;
        clGetProgramBuildInfo(g_program, g_device,
                              CL_PROGRAM_BUILD_LOG, 0, NULL, &logSize);
        char *log = (char *)malloc(logSize + 1);
        if (log) {
            clGetProgramBuildInfo(g_program, g_device,
                                  CL_PROGRAM_BUILD_LOG, logSize, log, NULL);
            log[logSize] = '\0';
            fprintf(stderr, "Build log:\n%s\n", log);
            free(log);
        }
        check_cl(err, "clBuildProgram");
    }

    g_kernel = clCreateKernel(g_program, "render_columns", &err);
    check_cl(err, "clCreateKernel");

    /* --- Bufferek letrehozasa --- */
    g_mapWallBuf = clCreateBuffer(g_context,
                                  CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                  sizeof(mapWall), mapWall, &err);
    check_cl(err, "clCreateBuffer mapWall");

    g_mapFloorBuf = clCreateBuffer(g_context,
                                   CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                   sizeof(mapFloor), mapFloor, &err);
    check_cl(err, "clCreateBuffer mapFloor");

    g_mapCeilingBuf = clCreateBuffer(g_context,
                                     CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                     sizeof(mapCeiling), mapCeiling, &err);
    check_cl(err, "clCreateBuffer mapCeiling");

    g_textureBuf = clCreateBuffer(g_context,
                                  CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                  TEXTURE_BYTES, (void *)All_Textures, &err);
    check_cl(err, "clCreateBuffer textures");

    g_skyBuf = clCreateBuffer(g_context,
                              CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                              SKY_BYTES, (void *)sky, &err);
    check_cl(err, "clCreateBuffer sky");

    g_outputBuf = clCreateBuffer(g_context,
                                 CL_MEM_WRITE_ONLY,
                                 sizeof(frameBuffer), NULL, &err);
    check_cl(err, "clCreateBuffer output");

    g_paramsBuf = clCreateBuffer(g_context,
                                 CL_MEM_READ_ONLY,
                                 sizeof(KernelParams), NULL, &err);
    check_cl(err, "clCreateBuffer params");
}

void cleanup_opencl(void)
{
    if (g_paramsBuf)    clReleaseMemObject(g_paramsBuf);
    if (g_outputBuf)    clReleaseMemObject(g_outputBuf);
    if (g_skyBuf)       clReleaseMemObject(g_skyBuf);
    if (g_textureBuf)   clReleaseMemObject(g_textureBuf);
    if (g_mapCeilingBuf)clReleaseMemObject(g_mapCeilingBuf);
    if (g_mapFloorBuf)  clReleaseMemObject(g_mapFloorBuf);
    if (g_mapWallBuf)   clReleaseMemObject(g_mapWallBuf);
    if (g_kernel)     clReleaseKernel(g_kernel);
    if (g_program)    clReleaseProgram(g_program);
    if (g_queue)      clReleaseCommandQueue(g_queue);
    if (g_context)    clReleaseContext(g_context);
}

void computeFrameOpenCL(void)
{
    size_t   globalSize = RAY_COUNT;
    cl_event kernelEvent;

    KernelParams params;
    params.player_x      = player_x;
    params.player_y      = player_y;
    params.player_DeltaX = player_DeltaX;
    params.player_DeltaY = player_DeltaY;
    params.angle         = angle;
    params.mapX          = mapX;
    params.mapY          = mapY;
    params.mapSum        = mapSum;
    params.screenW       = SCREEN_W;
    params.screenH       = SCREEN_H;
    params.rayCount      = RAY_COUNT;

    check_cl(
        clEnqueueWriteBuffer(g_queue, g_paramsBuf, CL_TRUE,
                             0, sizeof(params), &params,
                             0, NULL, NULL),
        "clEnqueueWriteBuffer params");

    check_cl(clSetKernelArg(g_kernel, 0, sizeof(cl_mem), &g_mapWallBuf),    "clSetKernelArg 0");
    check_cl(clSetKernelArg(g_kernel, 1, sizeof(cl_mem), &g_mapFloorBuf),   "clSetKernelArg 1");
    check_cl(clSetKernelArg(g_kernel, 2, sizeof(cl_mem), &g_mapCeilingBuf), "clSetKernelArg 2");
    check_cl(clSetKernelArg(g_kernel, 3, sizeof(cl_mem), &g_textureBuf),    "clSetKernelArg 3");
    check_cl(clSetKernelArg(g_kernel, 4, sizeof(cl_mem), &g_skyBuf),        "clSetKernelArg 4");
    check_cl(clSetKernelArg(g_kernel, 5, sizeof(cl_mem), &g_outputBuf),     "clSetKernelArg 5");
    check_cl(clSetKernelArg(g_kernel, 6, sizeof(cl_mem), &g_paramsBuf),     "clSetKernelArg 6");

    /* kernelEvent -> OpenCL feljegyzi a pontos GPU idot */
    check_cl(
        clEnqueueNDRangeKernel(g_queue, g_kernel, 1,
                               NULL, &globalSize, NULL,
                               0, NULL, &kernelEvent),
        "clEnqueueNDRangeKernel");

    check_cl(clFinish(g_queue), "clFinish");

    /* --- GPU kernel pontos ideje: clGetEventProfilingInfo (nanoszekundum) --- */
    cl_ulong t_start = 0, t_end = 0;
    clGetEventProfilingInfo(kernelEvent, CL_PROFILING_COMMAND_START,
                            sizeof(t_start), &t_start, NULL);
    clGetEventProfilingInfo(kernelEvent, CL_PROFILING_COMMAND_END,
                            sizeof(t_end),   &t_end,   NULL);
    clReleaseEvent(kernelEvent);

    double kernel_ms = (double)(t_end - t_start) * 1e-6;  /* ns -> ms */
    g_kernel_ms_sum += kernel_ms;

    check_cl(
        clEnqueueReadBuffer(g_queue, g_outputBuf, CL_TRUE,
                            0, sizeof(frameBuffer), frameBuffer,
                            0, NULL, NULL),
        "clEnqueueReadBuffer output");
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
            glColor3f(mapWall[y * mapX + x] > 0 ? 1.0f : 0.0f,
                      mapWall[y * mapX + x] > 0 ? 1.0f : 0.0f,
                      mapWall[y * mapX + x] > 0 ? 1.0f : 0.0f);
            int x0 = x * tileSize;
            int y0 = y * tileSize;
            glBegin(GL_QUADS);
                glVertex2i(x0,            y0);
                glVertex2i(x0,            y0 + tileSize);
                glVertex2i(x0 + tileSize, y0 + tileSize);
                glVertex2i(x0 + tileSize, y0);
            glEnd();
        }
    }
}

void display(void)
{
    /* --- Frame kezdete: idopont rogzitese --- */
    double frame_start_ms = (double)glutGet(GLUT_ELAPSED_TIME);

    frame2 = (float)frame_start_ms;
    fps    = frame2 - frame1;
    if (fps < 1.0f) fps = 1.0f;
    frame1 = frame2;

    int x0 = (player_DeltaX < 0.0f) ? -20 : 20;
    int y0 = (player_DeltaY < 0.0f) ? -20 : 20;

    int ipx        = (int)(player_x / 64.0f);
    int ipy        = (int)(player_y / 64.0f);
    int ipx_add_x0 = (int)((player_x + x0) / 64.0f);
    int ipx_sub_x0 = (int)((player_x - x0) / 64.0f);
    int ipy_add_y0 = (int)((player_y + y0) / 64.0f);
    int ipy_sub_y0 = (int)((player_y - y0) / 64.0f);

    if (Keys.w == 1) {
        if (mapWall[ipy        * mapX + ipx_add_x0] == 0) player_x += player_DeltaX * 0.2f * fps;
        if (mapWall[ipy_add_y0 * mapX + ipx]        == 0) player_y += player_DeltaY * 0.2f * fps;
    }
    if (Keys.s == 1) {
        if (mapWall[ipy        * mapX + ipx_sub_x0] == 0) player_x -= player_DeltaX * 0.2f * fps;
        if (mapWall[ipy_sub_y0 * mapX + ipx]        == 0) player_y -= player_DeltaY * 0.2f * fps;
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

    /* --- GPU compute (kernel_ms gyulik g_kernel_ms_sum-ban) --- */
    computeFrameOpenCL();

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawFrameBuffer();
    drawMap2D();
    drawplayer();
    glutSwapBuffers();

    /* --- Frame vege: teljes elteltt ido --- */
    double frame_ms = (double)glutGet(GLUT_ELAPSED_TIME) - frame_start_ms;
    g_frame_ms_sum += frame_ms;
    g_frame_count++;

    /* --- Statisztika kiirasa minden 60. frame-nel --- */
    if (g_frame_count % PRINT_EVERY == 0) {
        double avg_kernel = g_kernel_ms_sum / PRINT_EVERY;
        double avg_frame  = g_frame_ms_sum  / PRINT_EVERY;
        double avg_fps    = (avg_frame > 0.0) ? 1000.0 / avg_frame : 0.0;

        printf("[Frame %5u]  GPU kernel: %7.3f ms  |  "
               "Teljes frame: %7.3f ms  |  FPS: %6.1f\n",
               g_frame_count, avg_kernel, avg_frame, avg_fps);
        fflush(stdout);

        g_kernel_ms_sum = 0.0;
        g_frame_ms_sum  = 0.0;
    }

    glutPostRedisplay();
}

void ButtonDown(unsigned char key, int x, int y)
{
    (void)x; (void)y;
    if (key == 'a') Keys.a = 1;
    if (key == 'd') Keys.d = 1;
    if (key == 'w') Keys.w = 1;
    if (key == 's') Keys.s = 1;
    if (key == 'e') {
        int ex0 = (player_DeltaX < 0.0f) ? -25 : 25;
        int ey0 = (player_DeltaY < 0.0f) ? -25 : 25;
        int ipx_add = (int)((player_x + ex0) / 64.0f);
        int ipy_add = (int)((player_y + ey0) / 64.0f);
        if (mapWall[ipy_add * mapX + ipx_add] == 3) {
            mapWall[ipy_add * mapX + ipx_add] = 0;
            clEnqueueWriteBuffer(g_queue, g_mapWallBuf, CL_TRUE,
                                 0, sizeof(mapWall), mapWall,
                                 0, NULL, NULL);
        }
    }
}

void ButtonUp(unsigned char key, int x, int y)
{
    (void)x; (void)y;
    if (key == 'a') Keys.a = 0;
    if (key == 'd') Keys.d = 0;
    if (key == 'w') Keys.w = 0;
    if (key == 's') Keys.s = 0;
}

void resize(int w, int h)
{
    (void)w; (void)h;
    glutReshapeWindow(SCREEN_W, SCREEN_H);
}
