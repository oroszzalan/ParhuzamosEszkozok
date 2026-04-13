__kernel void render_columns(
    __global const int* mapWall,
    __global uchar* frame,
    int mapX,
    int mapY,
    int screenW,
    int screenH,
    float player_x,
    float player_y,
    float angle)
{
    int x = get_global_id(0);
    if (x >= screenW) return;

    float PI = 3.1415926535f;

    // FOV 60 fok
    float rayAngle = (angle - 30.0f) + ((float)x / screenW) * 60.0f;
    float rayDirX = cos(rayAngle * PI / 180.0f);
    float rayDirY = -sin(rayAngle * PI / 180.0f);

    float distance = 0.0f;
    int hit = 0;

    // egyszerű ray marching
    while (!hit && distance < 800.0f)
    {
        distance += 1.0f;

        int testX = (int)((player_x + rayDirX * distance) / 64.0f);
        int testY = (int)((player_y + rayDirY * distance) / 64.0f);

        if (testX < 0 || testY < 0 || testX >= mapX || testY >= mapY)
        {
            hit = 1;
            distance = 800.0f;
        }
        else
        {
            int index = testY * mapX + testX;
            if (mapWall[index] > 0)
            {
                hit = 1;
            }
        }
    }

    // halszem korrekció
    float ca = (angle - rayAngle) * PI / 180.0f;
    distance *= cos(ca);

    // oszlop magasság
    int lineHeight = (int)((mapX * screenH) / (distance + 0.0001f));
    if (lineHeight > screenH) lineHeight = screenH;

    int start = screenH / 2 - lineHeight / 2;
    int end   = screenH / 2 + lineHeight / 2;

    // rajzolás
    for (int y = 0; y < screenH; y++)
    {
        int idx = (y * screenW + x) * 3;

        if (y < start)
        {
            // ég
            frame[idx + 0] = 100;
            frame[idx + 1] = 150;
            frame[idx + 2] = 255;
        }
        else if (y > end)
        {
            // padló
            frame[idx + 0] = 50;
            frame[idx + 1] = 50;
            frame[idx + 2] = 50;
        }
        else
        {
            // fal (távolság alapú árnyékolás)
            float shade = 1.0f - (distance / 800.0f);
            if (shade < 0.2f) shade = 0.2f;

            frame[idx + 0] = (uchar)(0 * shade);
            frame[idx + 1] = (uchar)(255 * shade);
            frame[idx + 2] = (uchar)(0 * shade);
        }
    }
}