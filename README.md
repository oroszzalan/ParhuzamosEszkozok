# OpenCL Raycasting Renderer

Ez a projekt egy **raycasting alapú 3D megjelenítő**, ahol a kép kiszámítása OpenCL segítségével párhuzamosan történik.

A programban a CPU kezeli:
- a játékos mozgását,
- a billentyűzet inputot,
- az OpenGL-es megjelenítést.

Az OpenCL kernel számolja:
- a sugarakat,
- a falütközéseket,
- a falmagasságot,
- a textúrázott képoszlopokat,
- a framebuffer tartalmát.

---

# OpenCL működés a programban

Az OpenCL rész célja, hogy a raycasting számítás ne sorban, CPU-n történjen, hanem párhuzamosan.  
A programban minden sugár / képoszlop külön OpenCL work itemként fut.

## OpenCL inicializálás

Az `init_opencl()` függvény készíti elő az OpenCL környezetet.

### Platform és eszköz kiválasztása

```c
cl_uint platformCount = 0;
cl_uint deviceCount = 0;

check_cl(clGetPlatformIDs(1, &g_platform, &platformCount),
         "clGetPlatformIDs");

err = clGetDeviceIDs(g_platform, CL_DEVICE_TYPE_GPU,
                     1, &g_device, &deviceCount);

if (err != CL_SUCCESS || deviceCount == 0) {
    err = clGetDeviceIDs(g_platform, CL_DEVICE_TYPE_CPU,
                         1, &g_device, &deviceCount);
    check_cl(err, "clGetDeviceIDs CPU fallback");
}
```

Ez a rész először keres egy OpenCL platformot, majd GPU-t próbál választani.  
Ha nincs elérhető GPU, akkor CPU-ra vált vissza. Ez azért hasznos, mert így a program több gépen is futtatható.

---

## Context és Command Queue

```c
g_context = clCreateContext(NULL, 1, &g_device,
                            NULL, NULL, &err);
check_cl(err, "clCreateContext");

g_queue = clCreateCommandQueue(g_context, g_device, 0, &err);
check_cl(err, "clCreateCommandQueue");
```

A **context** az OpenCL futtatási környezete.  
A **command queue** az a sor, ahová a CPU beteszi a GPU-n végrehajtandó parancsokat.

Ilyen parancs például:
- buffer írás,
- kernel futtatás,
- eredmény visszaolvasás.

---

## Kernel fordítása és létrehozása

```c
g_program = clCreateProgramWithSource(g_context, 1,
                                      &kernelSource,
                                      NULL, &err);
check_cl(err, "clCreateProgramWithSource");

err = clBuildProgram(g_program, 1, &g_device,
                     NULL, NULL, NULL);

g_kernel = clCreateKernel(g_program,
                          "render_columns",
                          &err);
check_cl(err, "clCreateKernel");
```

Itt a program a kernel forráskódját betölti, lefordítja, majd létrehozza a `render_columns` nevű kernelt.

A kernel az a függvény, amely sok példányban, párhuzamosan fut az OpenCL eszközön.

---

## OpenCL buffer-ek

```c
g_mapWallBuf = clCreateBuffer(
    g_context,
    CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
    sizeof(mapWall),
    mapWall,
    &err
);

g_textureBuf = clCreateBuffer(
    g_context,
    CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
    TEXTURE_BYTES,
    (void*)All_Textures,
    &err
);

g_outputBuf = clCreateBuffer(
    g_context,
    CL_MEM_WRITE_ONLY,
    sizeof(frameBuffer),
    NULL,
    &err
);

g_paramsBuf = clCreateBuffer(
    g_context,
    CL_MEM_READ_ONLY,
    sizeof(KernelParams),
    NULL,
    &err
);
```

A CPU és az OpenCL eszköz külön memóriaterületet használ, ezért buffer-eket kell létrehozni.

| Buffer | Szerepe |
|---|---|
| `g_mapWallBuf` | a pálya fal adatai |
| `g_textureBuf` | textúrák |
| `g_outputBuf` | elkészült képkocka |
| `g_paramsBuf` | játékos és kamera paraméterei |

---

# Frame számítás OpenCL-lel

A `computeFrameOpenCL()` minden képkockánál lefut.

## Paraméterek előkészítése

```c
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
```

Ez a struktúra tartalmazza azokat az adatokat, amelyekre a kernelnek szüksége van:
- játékos pozíciója,
- nézési szög,
- pálya mérete,
- képernyő mérete,
- sugarak száma.

---

## Paraméter buffer feltöltése

```c
clEnqueueWriteBuffer(
    g_queue,
    g_paramsBuf,
    CL_TRUE,
    0,
    sizeof(params),
    &params,
    0,
    NULL,
    NULL
);
```

Ez átmásolja a CPU-n lévő paramétereket az OpenCL bufferbe.

---

## Kernel argumentumok beállítása

```c
clSetKernelArg(g_kernel, 0, sizeof(cl_mem), &g_mapWallBuf);
clSetKernelArg(g_kernel, 1, sizeof(cl_mem), &g_textureBuf);
clSetKernelArg(g_kernel, 2, sizeof(cl_mem), &g_outputBuf);
clSetKernelArg(g_kernel, 3, sizeof(cl_mem), &g_paramsBuf);
```

A kernel ezekből az argumentumokból dolgozik:
1. pálya,
2. textúrák,
3. kimeneti framebuffer,
4. paraméterek.

---

## Kernel futtatása

```c
size_t globalSize = RAY_COUNT;

clEnqueueNDRangeKernel(
    g_queue,
    g_kernel,
    1,
    NULL,
    &globalSize,
    NULL,
    0,
    NULL,
    NULL
);
```

A `globalSize = RAY_COUNT` azt jelenti, hogy annyi OpenCL work item indul, ahány sugarat számolunk.

Egyszerűen:

```text
1 sugár = 1 OpenCL szál
```

Ez a raycasting párhuzamosításának lényege.

---

## Eredmény visszaolvasása

```c
clEnqueueReadBuffer(
    g_queue,
    g_outputBuf,
    CL_TRUE,
    0,
    sizeof(frameBuffer),
    frameBuffer,
    0,
    NULL,
    NULL
);
```

A kernel által kitöltött képet a CPU visszaolvassa a `frameBuffer` tömbbe.  
Ezután az OpenGL ezt rajzolja ki.

---

# A kernel működése

A kernel neve:

```c
__kernel void render_columns(...)
```

Ez fut párhuzamosan minden sugárra.

---

## 1. Work item azonosító

```c
int r = get_global_id(0);

if (r >= params->rayCount) return;
```

A `get_global_id(0)` adja meg, hogy az adott kernelpéldány melyik sugárért felelős.

Például:
- `r = 0` → első sugár,
- `r = 1` → második sugár,
- `r = 100` → századik sugár.

---

## 2. Sugár szögének kiszámítása

```c
float ra = FixAng(
    params->angle + 30.0f
    - ((float)r * 60.0f / (float)params->rayCount)
);
```

A látószög 60 fok.  
A kernel minden sugárhoz kiszámolja a saját szögét.

A játékos nézési irányához képest:
- bal oldalon +30 fok,
- jobb oldalon -30 fok.

Így épül fel a teljes látómező.

---

## 3. Vertikális falmetszés keresése

```c
if (cos(degtorad(ra)) > 0.001f) {
    rayX = (((int)params->player_x >> 6) << 6) + 64.0f;
    rayY = (params->player_x - rayX) * Tan + params->player_y;
    xo = 64.0f;
    yo = -xo * Tan;
}
else if (cos(degtorad(ra)) < -0.001f) {
    rayX = (((int)params->player_x >> 6) << 6) - 0.0001f;
    rayY = (params->player_x - rayX) * Tan + params->player_y;
    xo = -64.0f;
    yo = -xo * Tan;
}
```

Ez a rész azt vizsgálja, hogy a sugár hol metszi a függőleges rácsvonalakat.

A pálya 64x64-es cellákból áll, ezért szerepel sok helyen a `64.0f`.

---

## 4. Fal ellenőrzése a map-ben

```c
while (dof < 8) {
    mapCoordinateX = ((int)rayX) >> 6;
    mapCoordinateY = ((int)rayY) >> 6;
    mapIndex = mapCoordinateY * params->mapX + mapCoordinateX;

    if (mapIndex > 0 &&
        mapIndex < params->mapX * params->mapY &&
        mapWall[mapIndex] > 0) {

        vmt = mapWall[mapIndex] - 1;
        dof = 8;
        disVertical =
            cos(degtorad(ra)) * (rayX - params->player_x)
            - sin(degtorad(ra)) * (rayY - params->player_y);
    }
    else {
        rayX += xo;
        rayY += yo;
        dof += 1;
    }
}
```

A kernel lépésenként halad a sugár mentén.  
Ha a `mapWall[mapIndex] > 0`, akkor falat talált.

A `disVertical` a vertikális találat távolsága.

---

## 5. Horizontális metszés

A kernel nem csak vertikális, hanem horizontális rácsvonalakat is vizsgál.

```c
if (sin(degtorad(ra)) > 0.001f) {
    rayY = (((int)params->player_y >> 6) << 6) - 0.0001f;
    rayX = (params->player_y - rayY) * Tan + params->player_x;
    yo = -64.0f;
    xo = -yo * Tan;
}
else if (sin(degtorad(ra)) < -0.001f) {
    rayY = (((int)params->player_y >> 6) << 6) + 64.0f;
    rayX = (params->player_y - rayY) * Tan + params->player_x;
    yo = 64.0f;
    xo = -yo * Tan;
}
```

Ez ugyanaz az elv, csak most vízszintes rácsvonalakkal.

A program végül összehasonlítja:
- vertikális távolság,
- horizontális távolság.

A kisebb távolság lesz a tényleges falütközés.

---

## 6. Közelebbi fal kiválasztása és árnyékolás

```c
float shade = 1.0f;

if (disVertical < disHorizontal) {
    hmt = vmt;
    shade = 0.5f;
    rayX = verticalX;
    rayY = verticalY;
    disHorizontal = disVertical;
}
```

Ha a vertikális találat közelebb van, akkor azt használja.  
Itt történik egy egyszerű árnyékolás is: bizonyos faloldalak sötétebbek lesznek.

Ez segít abban, hogy a kép térhatásosabb legyen.

---

## 7. Fisheye korrekció

```c
int ca = (int)FixAng(params->angle - ra);

disHorizontal =
    disHorizontal * cos(degtorad((float)ca));
```

A fisheye torzítás akkor jelenne meg, ha a szélső sugarak túl hosszúnak látszanának.  
A korrekció miatt a falak nem görbülnek, hanem egyenesnek látszanak.

---

## 8. Fal magasságának kiszámítása

```c
int lineH =
    (int)((params->mapSum * params->screenH) / disHorizontal);

if (lineH > params->screenH) {
    ty_off = (float)(lineH - params->screenH) / 2.0f;
    lineH = params->screenH;
}

int lineOff = (params->screenH / 2) - (lineH >> 1);
```

Minél közelebb van a fal, annál nagyobb lesz a kirajzolt oszlop.  
Minél távolabb van, annál kisebb.

Ez adja a 3D hatást.

---

## 9. Textúra X koordináta

```c
float tx;

if (shade == 1.0f) {
    tx = fmod(floor(rayX / 2.0f), 32.0f);
    if (ra > 180.0f) tx = 31.0f - tx;
}
else {
    tx = fmod(floor(rayY / 2.0f), 32.0f);
    if (ra > 90.0f && ra < 270.0f) tx = 31.0f - tx;
}

int texX = (int)tx;
```

A textúrák 32x32-esek.  
A `texX` megmondja, hogy a fal melyik textúraoszlopát kell használni.

---

## 10. Textúrázás és framebuffer írás

```c
for (int y = 0; y < params->screenH; ++y) {
    uchar red = 60, green = 60, blue = 60;

    if (y < lineOff) {
        red = 100;
        green = 160;
        blue = 220;
    }
    else if (y >= lineOff && y < lineOff + lineH) {
        float ty = ty_off * ty_step
                 + (float)(y - lineOff) * ty_step;

        int texY = (int)ty;

        int pixel =
            ((texY * 32) + texX) * 3
            + (hmt * 32 * 32 * 3);

        red   = (uchar)((float)textures[pixel + 0] * shade);
        green = (uchar)((float)textures[pixel + 1] * shade);
        blue  = (uchar)((float)textures[pixel + 2] * shade);
    }

    for (int x = xStart; x < xEnd; ++x) {
        int out = (y * params->screenW + x) * 3;

        output[out + 0] = red;
        output[out + 1] = green;
        output[out + 2] = blue;
    }
}
```

Ez a rész rajzolja ki a képoszlopot.

Három eset van:
- ég,
- fal,
- padló.

Ha falat rajzol, akkor a textúrából veszi ki a színt.  
A végén az `output` bufferbe írja az RGB értékeket.

---

# Egyszerűbb kernel részlet a külön `.cl` fájlból

A `raycasting_kernel.cl` fájlban egy egyszerűbb kernel is látható:

```c
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

    float rayAngle =
        (angle - 30.0f) + ((float)x / screenW) * 60.0f;

    float rayDirX = cos(rayAngle * PI / 180.0f);
    float rayDirY = -sin(rayAngle * PI / 180.0f);

    float distance = 0.0f;
    int hit = 0;

    while (!hit && distance < 800.0f) {
        distance += 1.0f;

        int testX =
            (int)((player_x + rayDirX * distance) / 64.0f);
        int testY =
            (int)((player_y + rayDirY * distance) / 64.0f);

        int index = testY * mapX + testX;

        if (mapWall[index] > 0) {
            hit = 1;
        }
    }
}
```

Ez a verzió ray marching módszert használ:  
a sugár kis lépésekben halad előre, amíg falat nem talál.

---

# Összefoglalás

A program OpenCL része így működik:

1. CPU előkészíti az adatokat.
2. OpenCL buffer-ekbe másolja őket.
3. Elindítja a `render_columns` kernelt.
4. Minden kernelpéldány egy sugárért felel.
5. A kernel kiszámolja a falütközést.
6. Meghatározza a fal magasságát.
7. Textúrából színt választ.
8. Beírja az eredményt az output bufferbe.
9. CPU visszaolvassa és OpenGL-lel kirajzolja.

A lényeg:

```text
raycasting = sok független sugár
OpenCL = sok párhuzamos szál
```

Ezért ez a feladat nagyon jól párhuzamosítható.
