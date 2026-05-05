typedef struct {
    float player_x;
    float player_y;
    float player_DeltaX;
    float player_DeltaY;
    float angle;
    int   mapX;
    int   mapY;
    int   mapSum;
    int   screenW;
    int   screenH;
    int   rayCount;
} KernelParams;

float degtorad(float a) { return a * 3.1415926535f / 180.0f; }

float FixAng(float a) {
    if (a > 359.0f) a -= 360.0f;
    if (a <   0.0f) a += 360.0f;
    return a;
}

__kernel void render_columns(
    __global const int*          mapWall,
    __global const int*          mapFloor,
    __global const int*          mapCeiling,
    __global const uchar*        textures,
    __global const uchar*        sky,
    __global       uchar*        output,
    __constant     KernelParams* params)
{
    int r = get_global_id(0);
    if (r >= params->rayCount) return;

    int   mapCoordinateX, mapCoordinateY, mapIndex, dof;
    float verticalX, verticalY, rayX, rayY, xo, yo;
    float disVertical, disHorizontal;
    int   vmt = 0, hmt = 0;

    float ra = FixAng(params->angle + 30.0f
                      - ((float)r * 60.0f / (float)params->rayCount));

    /* --- vertikalis metszespontok --- */
    dof = 0;
    disVertical = 100000.0f;
    float Tan = tan(degtorad(ra));

    if (cos(degtorad(ra)) > 0.001f) {
        rayX = (float)((((int)params->player_x >> 6) << 6) + 64);
        rayY = (params->player_x - rayX) * Tan + params->player_y;
        xo = 64.0f;  yo = -xo * Tan;
    } else if (cos(degtorad(ra)) < -0.001f) {
        rayX = (float)(((int)params->player_x >> 6) << 6) - 0.0001f;
        rayY = (params->player_x - rayX) * Tan + params->player_y;
        xo = -64.0f; yo = -xo * Tan;
    } else {
        rayX = params->player_x;
        rayY = params->player_y;
        dof  = 8;
    }

    while (dof < 8) {
        mapCoordinateX = ((int)rayX) >> 6;
        mapCoordinateY = ((int)rayY) >> 6;
        mapIndex = mapCoordinateY * params->mapX + mapCoordinateX;
        if (mapIndex > 0 && mapIndex < params->mapX * params->mapY
            && mapWall[mapIndex] > 0) {
            vmt = mapWall[mapIndex] - 1;
            dof = 8;
            disVertical = cos(degtorad(ra)) * (rayX - params->player_x)
                        - sin(degtorad(ra)) * (rayY - params->player_y);
        } else { rayX += xo; rayY += yo; dof++; }
    }
    verticalX = rayX; verticalY = rayY;

    /* --- horizontalis metszespontok --- */
    dof = 0;
    disHorizontal = 100000.0f;
    if (fabs(Tan) < 0.000001f) Tan = 0.000001f;
    Tan = 1.0f / Tan;

    if (sin(degtorad(ra)) > 0.001f) {
        rayY = (float)(((int)params->player_y >> 6) << 6) - 0.0001f;
        rayX = (params->player_y - rayY) * Tan + params->player_x;
        yo = -64.0f; xo = -yo * Tan;
    } else if (sin(degtorad(ra)) < -0.001f) {
        rayY = (float)(((int)params->player_y >> 6) << 6) + 64.0f;
        rayX = (params->player_y - rayY) * Tan + params->player_x;
        yo = 64.0f;  xo = -yo * Tan;
    } else {
        rayX = params->player_x;
        rayY = params->player_y;
        dof  = 8;
    }

    while (dof < 8) {
        mapCoordinateX = ((int)rayX) >> 6;
        mapCoordinateY = ((int)rayY) >> 6;
        mapIndex = mapCoordinateY * params->mapX + mapCoordinateX;
        if (mapIndex > 0 && mapIndex < params->mapX * params->mapY
            && mapWall[mapIndex] > 0) {
            hmt = mapWall[mapIndex] - 1;
            dof = 8;
            disHorizontal = cos(degtorad(ra)) * (rayX - params->player_x)
                          - sin(degtorad(ra)) * (rayY - params->player_y);
        } else { rayX += xo; rayY += yo; dof++; }
    }

    /* --- kozelebbi fal + arnyekolas --- */
    float shade = 1.0f;
    if (disVertical < disHorizontal) {
        hmt   = vmt;
        shade = 0.5f;
        rayX  = verticalX;
        rayY  = verticalY;
        disHorizontal = disVertical;
    }

    /* --- fisheye korrektio --- */
    int ca = (int)FixAng(params->angle - ra);
    disHorizontal *= cos(degtorad((float)ca));
    if (disHorizontal < 0.0001f) disHorizontal = 0.0001f;

    /* --- fal magassaga --- */
    int lineH = (int)((params->mapSum * params->screenH) / disHorizontal);
    float ty_step = 32.0f / (float)(lineH > 0 ? lineH : 1);
    float ty_off  = 0.0f;
    if (lineH > params->screenH) {
        ty_off = (float)(lineH - params->screenH) / 2.0f;
        lineH  = params->screenH;
    }
    int lineOff = (params->screenH / 2) - (lineH >> 1);

    /* --- textura X --- */
    float tx;
    if (shade == 1.0f) {
        tx = fmod(floor(rayX / 2.0f), 32.0f);
        if (ra > 180.0f) tx = 31.0f - tx;
    } else {
        tx = fmod(floor(rayY / 2.0f), 32.0f);
        if (ra > 90.0f && ra < 270.0f) tx = 31.0f - tx;
    }
    if (tx < 0.0f) tx += 32.0f;
    int texX = (int)tx;
    if (texX < 0)  texX = 0;
    if (texX > 31) texX = 31;

    int xStart = r       * params->screenW / params->rayCount;
    int xEnd   = (r + 1) * params->screenW / params->rayCount;

    /* --- keposzlop kirajzolasa --- */
    for (int y = 0; y < params->screenH; ++y) {
        uchar red = 60, green = 60, blue = 60;

        if (y < lineOff) {
            /* ---- EG ---- */
            int skyY = (lineOff > 0) ? (y * 40 / lineOff) : 0;
            if (skyY > 39) skyY = 39;
            int skyX = ((int)(params->angle * 2.0f) - (xStart * 120 / params->screenW) + 120) % 120;
            int skyPix = (skyY * 120 + skyX) * 3;
            red   = sky[skyPix + 0];
            green = sky[skyPix + 1];
            blue  = sky[skyPix + 2];

        } else if (y < lineOff + lineH) {
            /* ---- FAL ---- */
            float fty = ty_off * ty_step + (float)(y - lineOff) * ty_step;
            int texY = (int)fty;
            if (texY < 0)  texY = 0;
            if (texY > 31) texY = 31;
            int pixel = ((texY * 32) + texX) * 3 + (hmt * 32 * 32 * 3);
            red   = (uchar)((float)textures[pixel + 0] * shade);
            green = (uchar)((float)textures[pixel + 1] * shade);
            blue  = (uchar)((float)textures[pixel + 2] * shade);

        } else {
            /* ---- PADLO ---- */
            float dy    = (float)y - (float)(params->screenH / 2);
            float deg   = degtorad(ra);
            float raFix = cos(degtorad(FixAng(params->angle - ra)));
            if (fabs(raFix) < 0.0001f) raFix = 0.0001f;
            if (fabs(dy)    < 0.0001f) dy    = 0.0001f;

            float ftx = params->player_x / 2.0f + cos(deg) * 158.0f * 32.0f / dy / raFix;
            float fty = params->player_y / 2.0f - sin(deg) * 158.0f * 32.0f / dy / raFix;

            int mpF = mapFloor[((int)(fty / 32.0f)) * params->mapX + ((int)(ftx / 32.0f))] * 32 * 32;
            int pixF = ((((int)fty & 31) * 32) + ((int)ftx & 31)) * 3 + mpF * 3;
            red   = (uchar)((float)textures[pixF + 0] * 0.7f);
            green = (uchar)((float)textures[pixF + 1] * 0.7f);
            blue  = (uchar)((float)textures[pixF + 2] * 0.7f);

            /* ---- PLAFON (tukorsor) ---- */
            int ceilY = params->screenH - 1 - y;
            if (ceilY >= 0) {
                int mpC = mapCeiling[((int)(fty / 32.0f)) * params->mapX + ((int)(ftx / 32.0f))] * 32 * 32;
                if (mpC > 0) {
                    int pixC = ((((int)fty & 31) * 32) + ((int)ftx & 31)) * 3 + mpC * 3;
                    for (int x = xStart; x < xEnd; ++x) {
                        int outC = (ceilY * params->screenW + x) * 3;
                        output[outC + 0] = textures[pixC + 0];
                        output[outC + 1] = textures[pixC + 1];
                        output[outC + 2] = textures[pixC + 2];
                    }
                }
            }
        }

        for (int x = xStart; x < xEnd; ++x) {
            int out = (y * params->screenW + x) * 3;
            output[out + 0] = red;
            output[out + 1] = green;
            output[out + 2] = blue;
        }
    }
}
