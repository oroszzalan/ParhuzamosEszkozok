#include "raycasting_opencl.h"
#include <stdlib.h>

int main(int argc, char* argv[])
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(SCREEN_W, SCREEN_H);
    glutInitWindowPosition(200, 200);
    glutCreateWindow("OpenCL Raycasting");

    init();
    init_opencl();

    atexit(cleanup_opencl);

    glutDisplayFunc(display);
    glutReshapeFunc(resize);
    glutKeyboardFunc(ButtonDown);
    glutKeyboardUpFunc(ButtonUp);

    glutMainLoop();
    return 0;
}