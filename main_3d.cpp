#include <windows.h>
#include <GL/glut.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <mmsystem.h>
#include <iostream>
#include <cstring>
using namespace std;

// Some older/minimal MinGW GL/gl.h headers (pre-OpenGL-1.3) don't ship the
// GL_MULTISAMPLE enum even though the driver supports it at runtime -
// define it manually (its value is fixed by the OpenGL spec) if missing.
#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE 0x809D
#endif

#define PI 3.14159265358979323846f

// ─────────────────────────────────────────────────────────────
//  S MATH  HELPERS
//  Lerp:  Result = start + t * (end – start)
// ─────────────────────────────────────────────────────────────
inline float lerpF(float start, float end, float t)
{
    return start + t * (end - start);
}
inline float clampF(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}
inline float randF(float lo, float hi)
{
    return lo + (hi - lo) * ((float)rand() / (float)RAND_MAX);
}

// ─────────────────────────────────────────────────────────────
//  S ORIGINAL  ANIMATION  STATE  (unchanged)
// ─────────────────────────────────────────────────────────────
int     frameNumber = 1;

GLfloat position_b1 = -0.5f,  speed_b1 = 0.002f;
GLfloat position_b2 = -1.5f,  speed_b2 = 0.002f;
GLfloat position_r  = -0.1f,  speed_r  = 0.01f;
GLfloat position_s  =  1.6f,  speed_s  = 0.01f;

GLfloat position_c1 =  1.6f,  speed_c1 = 0.01f;
GLfloat position_c2 =  2.4f,  speed_c2 = 0.01f;
GLfloat position_c3 = -0.9f,  speed_c3 = 0.01f;
GLfloat position_c4 =  0.9f,  speed_c4 = 0.01f;

GLfloat position_rain  = 0.0f, speed_rain  = 0.05f;
GLfloat position_rain2 = 2.0f, speed_rain2 = 0.05f;

// Quick win: wheel rotation. Each car's accumulated spin angle (degrees),
// advanced every frame in proportion to how far it actually travelled.
float wheelSpin1 = 0.0f, wheelSpin2 = 0.0f, wheelSpin3 = 0.0f, wheelSpin4 = 0.0f;
static const float WHEEL_RADIUS = 0.065f;

// Quick win: animated river. Advances the sine-wave phase of the water mesh.
float riverWaveOffset = 0.0f;

int cnt = 0;   // 0 = horizontal green / vertical red,  >0 = horizontal red / vertical green
int flag = 0;  // 0 = day,          >0 = night   (legacy toggle)
int r    = 0;  // 0 = no rain,      >0 = raining

// ─────────────────────────────────────────────────────────────
//  § 2A  REALISTIC TRAFFIC SIGNAL STATE MACHINE
//  Real intersections never jump straight from green to red -
//  they hold a short amber "caution" phase, and briefly hold
//  BOTH directions red (all-red clearance) so the intersection
//  empties before the cross traffic gets its green. This timer
//  drives that automatically; 'r' can still force an early
//  transition and 'g' still forces an immediate reset to
//  horizontal-green.
// ─────────────────────────────────────────────────────────────
bool  signalYellow      = false;  // true while the currently-green side is amber
bool  signalAllRed      = false;  // true during the brief all-red clearance gap
float signalClock       = 0.0f;   // seconds elapsed in the current sub-phase
bool  signalAuto        = true;   // auto-cycle the light on a timer

// § 2B  ADAPTIVE / DENSITY-BASED TIMING
// Instead of a fixed green duration, this behaves like a real
// vehicle-actuated signal: it holds green at least MIN_GREEN, extends
// green as long as a car is still using (approaching/crossing) that
// road, and only hands over once the road is clear AND the other road
// actually has a car waiting - capped at MAX_GREEN so one direction
// can never starve the other.
const float SIGNAL_MIN_GREEN   = 3.0f;   // never switch before this
const float SIGNAL_MAX_GREEN   = 12.0f;  // always switch by this (fairness cap)
const float SIGNAL_YELLOW_TIME = 1.6f;   // amber caution duration
const float SIGNAL_ALLRED_TIME = 0.6f;   // both-red clearance gap
const float DETECT_AHEAD  = 1.0f;  // "approaching" detection distance before the stop line
const float DETECT_BEHIND = 0.5f;  // still counted as "using the intersection" just past the line

// ─────────────────────────────────────────────────────────────
//  S 1A  GOLDEN HOUR / SKY TRANSITION
//  skyPhase: 0.0 = Day,  1.0 = Sunset,  2.0 = Night
//  Lerps glClearColor + skybox colours smoothly between phases.
// ─────────────────────────────────────────────────────────────
float skyPhase        = 0.0f;   // current phase (continuous)
float skyPhaseTarget  = 0.0f;   // where we want to go
bool  skyAutoAdvance  = false;  // 'T' key cycles automatically
float skyPhaseTimer   = 0.0f;  // accumulates time per auto-phase

// Palette keyframes  – [0]=Day  [1]=Sunset  [2]=Night
// Each row: { R, G, B }
static const float SKY_ZENITH[3][3] = {
    { 0.30f, 0.55f, 0.95f },   // Day    zenith (deep blue)
    { 0.65f, 0.22f, 0.08f },   // Sunset zenith (burnt orange)
    { 0.02f, 0.02f, 0.10f },   // Night  zenith (near-black blue)
};
static const float SKY_HORIZON[3][3] = {
    { 0.55f, 0.80f, 1.00f },   // Day    horizon (sky blue)
    { 1.00f, 0.50f, 0.20f },   // Sunset horizon (orange)
    { 0.05f, 0.05f, 0.20f },   // Night  horizon (indigo)
};

// Compute interpolated sky colour at an arbitrary phase value
// phase wraps 0→3 with three keyframes
static void evalSkyColour(float phase,
                           float outZenith[3], float outHorizon[3])
{
    // Wrap into [0, 3)
    while (phase >= 3.0f) phase -= 3.0f;
    while (phase <  0.0f) phase += 3.0f;

    int   lo  = (int)phase % 3;
    int   hi  = (lo + 1) % 3;
    float frac = phase - floorf(phase);

    for (int i = 0; i < 3; i++) {
        outZenith [i] = lerpF(SKY_ZENITH [lo][i], SKY_ZENITH [hi][i], frac);
        outHorizon[i] = lerpF(SKY_HORIZON[lo][i], SKY_HORIZON[hi][i], frac);
    }
}

// ─────────────────────────────────────────────────────────────
//  S 1B  VOLUMETRIC FOG
// ─────────────────────────────────────────────────────────────
bool  isRaining         = false;
float currentFogDensity = 0.0f;
float targetFogDensity  = 0.0f;

// ─────────────────────────────────────────────────────────────
//  § 1C  PROCEDURAL CLOUDS
// ─────────────────────────────────────────────────────────────
float cloudOffset = 0.0f;          // advances every frame

// Cloud descriptor – position + scale radii
struct CloudDesc { float x, y, z, rx, rz; };
static const CloudDesc CLOUDS[] = {
    { -3.0f, 3.2f,  1.0f, 0.70f, 0.50f },
    { -0.5f, 3.6f, -0.5f, 0.55f, 0.45f },
    {  1.8f, 3.0f,  0.8f, 0.90f, 0.60f },
    {  3.5f, 3.4f, -1.0f, 0.65f, 0.40f },
    { -2.2f, 2.9f, -1.5f, 0.75f, 0.55f },
};
static const int NUM_CLOUDS = 5;

// ─────────────────────────────────────────────────────────────
//  § 2  LIVING INFRASTRUCTURE
// ─────────────────────────────────────────────────────────────
bool powerOutage = false;   // 'P' key – kills all lights/windows
bool isNight     = false;   // derived from skyPhase each frame

// Gradual braking: distance at which a car starts slowing
static const float SENSOR_RANGE  = 0.60f;
// Positions cars must stop before when light is red
static const float STOP_C1 =  0.05f;   // car1 stop-line (x)
static const float STOP_C2 =  1.75f;  // car2 stop-line (x)
static const float STOP_C3 =  1.65f;  // car3 stop-line (z)
static const float STOP_C4 = -1.65f;  // car4 stop-line (z)

// ─────────────────────────────────────────────────────────────
//  § 3  PARTICLE  SYSTEMS
// ─────────────────────────────────────────────────────────────
struct Particle {
    float x, y, z;      // world position
    float vx, vy, vz;   // velocity (units/frame)
    float size;          // sphere/quad radius
    float alpha;         // current transparency
    float life;          // normalised life 1.0→0.0
    bool  active;
};

// Chimney smoke (50 per chimney × 3 chimneys = 150 total)
static const int NUM_CHIMNEYS        = 3;
static const float CHIMNEY_X[3]      = { 2.9f, 3.3f, 3.7f };
static const float CHIMNEY_Z_COORD   = -1.0f;
static const float CHIMNEY_Y_BASE    =  1.35f;
static const int   SMOKE_PER_CHIMNEY =  50;
static const int   MAX_SMOKE         = NUM_CHIMNEYS * SMOKE_PER_CHIMNEY;
Particle smokeParticles[MAX_SMOKE];

// Additional base-smoke for meteor-destroyed buildings (30 particles)
static const int MAX_BASE_SMOKE = 30;
Particle baseSmokeParticles[MAX_BASE_SMOKE];

// Rain splashes
static const int MAX_SPLASH = 80;
Particle splashParticles[MAX_SPLASH];

// ─────────────────────────────────────────────────────────────
//  § 4  GOD MODE  –  Building State
// ─────────────────────────────────────────────────────────────
static const int NUM_BUILDINGS = 6;

struct BuildingState {
    float heightScale;       // 1.0 = normal, lerps → 0.0 on destroy
    int   colorCycle;        // index into BWALL_COLORS
    bool  isDestroyed;
    float destroyProgress;   // 0.0 → 1.0 (drive by timer)
    bool  emittingBaseSmoke;
};

BuildingState bState[NUM_BUILDINGS] = {
    { 1.0f, 0, false, 0.0f, false },  // 0 – Left house
    { 1.0f, 0, false, 0.0f, false },  // 1 – Right house
    { 1.0f, 0, false, 0.0f, false },  // 2 – BFC shop
    { 1.0f, 0, false, 0.0f, false },  // 3 – Candy shop
    { 1.0f, 0, false, 0.0f, false },  // 4 – Factory
    { 1.0f, 0, false, 0.0f, false },  // 5 – Windmill
};

bool meteorMode = false;

// Cycling wall-colour presets (R, G, B)
static const float BWALL_COLORS[5][3] = {
    { 0.88f, 0.89f, 0.90f },   // 0 – original grey-white
    { 1.00f, 0.85f, 0.65f },   // 1 – warm sandstone
    { 0.65f, 0.90f, 0.70f },   // 2 – sage green
    { 0.90f, 0.72f, 0.88f },   // 3 – lavender
    { 1.00f, 1.00f, 0.76f },   // 4 – pale cream
};

// Base world-X / world-Z for each building (matches PICK_TARGETS)
static const float BLDG_CX[6] = { -2.3f, 0.5f, -3.3f, -1.0f, 3.3f, 3.2f };
static const float BLDG_CZ[6] = { -1.5f,-1.5f, -1.5f, -1.5f,-0.8f,-0.5f };

// ═══════════════════════════════════════════════════════════
//  CAMERA STATE STRUCT  (unchanged from v2)
// ═══════════════════════════════════════════════════════════
struct Camera
{
    float radius, azimuth, elevation;
    float homeRadius, homeAzimuth, homeElevation;
    float minElevation, maxElevation, minRadius, maxRadius;
    float velAz, velEl, damping, velEpsilon;
    bool  dragging;
    int   lastMX, lastMY;
    float dragSens;
    bool  autoRotate;
    float autoSpeed;
    bool  lerpHome;
    float lerpSpeed;

    void getPosition(float& cx, float& cy, float& cz) const {
        float az = azimuth   * PI / 180.0f;
        float el = elevation * PI / 180.0f;
        cx = radius * cosf(el) * sinf(az);
        cy = radius * sinf(el);
        cz = radius * cosf(el) * cosf(az);
    }
    void clamp() {
        if (elevation < minElevation) elevation = minElevation;
        if (elevation > maxElevation) elevation = maxElevation;
        if (radius    < minRadius)    radius    = minRadius;
        if (radius    > maxRadius)    radius    = maxRadius;
    }
    void tick() {
        bool dirty = false;
        if (lerpHome) {
            float dAz=homeAzimuth-azimuth, dEl=homeElevation-elevation, dR=homeRadius-radius;
            if (fabsf(dAz)<0.25f && fabsf(dEl)<0.25f && fabsf(dR)<0.03f) {
                azimuth=homeAzimuth; elevation=homeElevation; radius=homeRadius; lerpHome=false;
            } else { azimuth+=dAz*lerpSpeed; elevation+=dEl*lerpSpeed; radius+=dR*lerpSpeed; dirty=true; }
            clamp();
        }
        if (autoRotate && !lerpHome) { azimuth+=autoSpeed; dirty=true; }
        if (!dragging && !lerpHome) {
            if (fabsf(velAz)>velEpsilon || fabsf(velEl)>velEpsilon) {
                azimuth+=velAz; elevation+=velEl; clamp();
                velAz*=damping; velEl*=damping;
                if (fabsf(velAz)<velEpsilon) velAz=0.0f;
                if (fabsf(velEl)<velEpsilon) velEl=0.0f;
                dirty=true;
            }
        }
        if (dirty) glutPostRedisplay();
    }
};

static Camera cam = {
    9.3f,36.87f,36.26f,
    9.3f,36.87f,36.26f,
    5.0f,85.0f,2.5f,28.0f,
    0.0f,0.0f,0.88f,0.003f,
    false,0,0,0.35f,
    false,0.18f,
    false,0.06f
};

// ── Hover / Picking (unchanged) ──────────────────────────────
struct PickTarget { float cx, cz, radius; };
static const PickTarget PICK_TARGETS[] = {
    {-2.3f,-1.5f,0.80f},{0.5f,-1.5f,0.70f},{-3.3f,-1.5f,0.65f},
    {-1.0f,-1.5f,0.70f},{3.3f,-0.8f,0.90f},{3.2f,-0.5f,0.50f},
};
static const int NUM_PICK = 6;

struct HighlightBox { float cx, cz, w, d, h; };
static const HighlightBox HIGHLIGHT_BOXES[] = {
    {-2.3f,-1.5f,1.20f,1.20f,1.40f},{0.5f,-1.5f,1.00f,1.00f,1.40f},
    {-3.3f,-1.5f,1.00f,0.80f,0.70f},{-1.0f,-1.5f,1.30f,0.80f,0.80f},
    {3.3f,-0.9f,1.20f,0.80f,1.20f},{3.2f,-0.5f,0.50f,0.50f,1.40f},
};
static const char* HOVER_NAMES[] = {
    "Left House","Right House","BFC Shop","Candy Shop","Factory","Windmill"
};

static int hoveredObj = -1;
static int mouseX     = 715;
static int mouseY     = 400;

// ═══════════════════════════════════════════════════════════
//  HELPER  PRIMITIVES  (unchanged)
// ═══════════════════════════════════════════════════════════
static void setMat(GLfloat r, GLfloat g, GLfloat b, GLfloat a = 1.0f)
{
    GLfloat diff[4]={r,g,b,a}, amb[4]={r*0.35f,g*0.35f,b*0.35f,a};
    GLfloat spec[4]={0.4f,0.4f,0.4f,1.0f};
    GLfloat emis[4]={0.0f,0.0f,0.0f,1.0f};   // always reset emission so it never
                                              // leaks onto whatever is drawn next
    glMaterialfv(GL_FRONT_AND_BACK,GL_AMBIENT,  amb);
    glMaterialfv(GL_FRONT_AND_BACK,GL_DIFFUSE,  diff);
    glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR, spec);
    glMaterialf (GL_FRONT_AND_BACK,GL_SHININESS,32.0f);
    glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION, emis);
    glColor4f(r,g,b,a);
}

// Power-outage-aware window colour helper.
// At night (and not powered-out) the window is made emissive so it reads
// as a genuinely glowing, lit-up pane even under low night-time ambient
// light, instead of just a flat-shaded coloured box.
static void setWindowMat(GLfloat r, GLfloat g, GLfloat b)
{
    if (powerOutage) { setMat(0.07f,0.07f,0.07f); return; }
    setMat(r,g,b);
    if (isNight) {
        GLfloat em[4] = { r*0.85f, g*0.85f, b*0.55f, 1.0f };
        glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, em);
    }
}

static void drawBox(float w, float h, float d)
{
    glPushMatrix(); glScalef(w,h,d); glutSolidCube(1.0); glPopMatrix();
}
static void flatQuad(float x0, float z0, float x1, float z1, float y=0.0f)
{
    glNormal3f(0,1,0);
    glBegin(GL_QUADS);
        glVertex3f(x0,y,z0); glVertex3f(x1,y,z0);
        glVertex3f(x1,y,z1); glVertex3f(x0,y,z1);
    glEnd();
}
static void drawCylinder(float radius, float height, int slices=16)
{
    GLUquadric *q=gluNewQuadric(); gluQuadricNormals(q,GLU_SMOOTH);
    glPushMatrix(); glRotatef(-90,1,0,0);
    gluCylinder(q,radius,radius,height,slices,1);
    gluDisk(q,0,radius,slices,1);
    glTranslatef(0,0,height); gluDisk(q,0,radius,slices,1);
    glPopMatrix(); gluDeleteQuadric(q);
}

// ─────────────────────────────────────────────────────────────
//  § 5  TECHNICAL POLISH  –  Fake Shadow helper
//  Draws a dark-grey semi-transparent oval/rect at y=0.001
//  under any car or boat.  Call BEFORE drawing the object.
// ─────────────────────────────────────────────────────────────
static void drawFakeShadow(float cx, float cz, float w, float d)
{
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glColor4f(0.2f, 0.2f, 0.2f, 0.50f);
    glNormal3f(0, 1, 0);
    glBegin(GL_QUADS);
        glVertex3f(cx - w*0.5f, 0.001f, cz - d*0.5f);
        glVertex3f(cx + w*0.5f, 0.001f, cz - d*0.5f);
        glVertex3f(cx + w*0.5f, 0.001f, cz + d*0.5f);
        glVertex3f(cx - w*0.5f, 0.001f, cz + d*0.5f);
    glEnd();

    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

// ═══════════════════════════════════════════════════════════
//  GROUND  (unchanged)
// ═══════════════════════════════════════════════════════════
void drawGround()
{
    const float HW=4.0f, THICK=0.55f, TOP=0.0f, BOT=-THICK;
    setMat(0.22f,0.55f,0.18f); flatQuad(-HW,-HW,HW,HW,TOP);
    setMat(0.30f,0.65f,0.22f);
    const float T=0.12f;
    flatQuad(-HW,-HW,-HW+T,HW,0.001f); flatQuad(HW-T,-HW,HW,HW,0.001f);
    flatQuad(-HW+T,-HW,HW-T,-HW+T,0.001f); flatQuad(-HW+T,HW-T,HW-T,HW,0.001f);
    setMat(0.50f,0.31f,0.13f);
    glNormal3f(0,0,1); glBegin(GL_QUADS); glVertex3f(-HW,TOP,HW); glVertex3f(HW,TOP,HW); glVertex3f(HW,BOT,HW); glVertex3f(-HW,BOT,HW); glEnd();
    glNormal3f(0,0,-1); glBegin(GL_QUADS); glVertex3f(HW,TOP,-HW); glVertex3f(-HW,TOP,-HW); glVertex3f(-HW,BOT,-HW); glVertex3f(HW,BOT,-HW); glEnd();
    glNormal3f(1,0,0); glBegin(GL_QUADS); glVertex3f(HW,TOP,-HW); glVertex3f(HW,TOP,HW); glVertex3f(HW,BOT,HW); glVertex3f(HW,BOT,-HW); glEnd();
    glNormal3f(-1,0,0); glBegin(GL_QUADS); glVertex3f(-HW,TOP,HW); glVertex3f(-HW,TOP,-HW); glVertex3f(-HW,BOT,-HW); glVertex3f(-HW,BOT,HW); glEnd();
    setMat(0.40f,0.24f,0.09f);
    const float MID=BOT+THICK*0.5f;
    glNormal3f(0,0,1); glBegin(GL_QUADS); glVertex3f(-HW,MID,HW); glVertex3f(HW,MID,HW); glVertex3f(HW,BOT,HW); glVertex3f(-HW,BOT,HW); glEnd();
    glNormal3f(0,0,-1); glBegin(GL_QUADS); glVertex3f(HW,MID,-HW); glVertex3f(-HW,MID,-HW); glVertex3f(-HW,BOT,-HW); glVertex3f(HW,BOT,-HW); glEnd();
    glNormal3f(1,0,0); glBegin(GL_QUADS); glVertex3f(HW,MID,-HW); glVertex3f(HW,MID,HW); glVertex3f(HW,BOT,HW); glVertex3f(HW,BOT,-HW); glEnd();
    glNormal3f(-1,0,0); glBegin(GL_QUADS); glVertex3f(-HW,MID,HW); glVertex3f(-HW,MID,-HW); glVertex3f(-HW,BOT,-HW); glVertex3f(-HW,BOT,HW); glEnd();
    setMat(0.30f,0.18f,0.07f);
    glNormal3f(0,-1,0); glBegin(GL_QUADS); glVertex3f(-HW,BOT,-HW); glVertex3f(HW,BOT,-HW); glVertex3f(HW,BOT,HW); glVertex3f(-HW,BOT,HW); glEnd();
    setMat(0.25f,0.15f,0.06f);
    for (int i=0;i<5;i++) {
        float fx=-3.0f+i*1.4f;
        glNormal3f(0,0,-1); glBegin(GL_QUADS);
            glVertex3f(fx,BOT+0.12f,-HW-0.001f); glVertex3f(fx+0.08f,BOT+0.12f,-HW-0.001f);
            glVertex3f(fx+0.08f,BOT,-HW-0.001f); glVertex3f(fx,BOT,-HW-0.001f);
        glEnd();
    }
}

// ═══════════════════════════════════════════════════════════
//  ROADS  (unchanged)
// ═══════════════════════════════════════════════════════════
void drawRoads()
{
    const float Y=0.01f;
    setMat(0.37f,0.38f,0.36f);
    flatQuad(-4.0f,-1.0f,4.0f,1.0f,Y); flatQuad(0.8f,-4.0f,2.8f,4.0f,Y);
    setMat(0.69f,0.75f,0.74f);
    flatQuad(-4.0f,0.6f,1.2f,1.0f,Y+0.005f); flatQuad(-4.0f,-1.0f,1.2f,-0.6f,Y+0.005f);
    flatQuad(2.4f,0.6f,4.0f,1.0f,Y+0.005f);  flatQuad(2.4f,-1.0f,4.0f,-0.6f,Y+0.005f);
    flatQuad(0.8f,-4.0f,1.2f,-1.0f,Y+0.005f); flatQuad(2.4f,-4.0f,2.8f,-1.0f,Y+0.005f);
    flatQuad(0.8f,1.0f,1.2f,4.0f,Y+0.005f);   flatQuad(2.4f,1.0f,2.8f,4.0f,Y+0.005f);
    setMat(1.0f,1.0f,1.0f);
    for (int i=-3;i<=3;i++) {
        if (i==0||i==1) continue;
        glPushMatrix(); glTranslatef((float)i*1.1f,Y+0.01f,0.0f); drawBox(0.4f,0.01f,0.04f); glPopMatrix();
    }
    for (int i=-3;i<=3;i++) { glPushMatrix(); glTranslatef(1.8f,Y+0.01f,(float)i*1.1f); drawBox(0.04f,0.01f,0.4f); glPopMatrix(); }
    for (int i=0;i<5;i++) {
        float zz=-0.8f+i*0.35f;
        glPushMatrix(); glTranslatef(0.6f,Y+0.01f,zz);  drawBox(0.4f,0.01f,0.12f); glPopMatrix();
        glPushMatrix(); glTranslatef(3.0f,Y+0.01f,zz);  drawBox(0.4f,0.01f,0.12f); glPopMatrix();
    }
    for (int i=0;i<5;i++) {
        float xx=1.0f+i*0.35f;
        glPushMatrix(); glTranslatef(xx,Y+0.01f,1.2f);  drawBox(0.12f,0.01f,0.4f); glPopMatrix();
        glPushMatrix(); glTranslatef(xx,Y+0.01f,-1.2f); drawBox(0.12f,0.01f,0.4f); glPopMatrix();
    }
}

// ═══════════════════════════════════════════════════════════
//  BUILDINGS  –  modified for BuildingState (color + destroy)
// ═══════════════════════════════════════════════════════════

// Draws a generic building.  wr/wg/wb are DEFAULT wall colours
// (overridden by bState[bIdx].colorCycle when bIdx >= 0).
void drawBuilding(float cx, float cz, float w, float d, float h,
                  GLfloat wr, GLfloat wg, GLfloat wb,
                  GLfloat rr, GLfloat rg, GLfloat rb,
                  int bIdx = -1)
{
    // Apply cycled wall colour
    if (bIdx >= 0) {
        int c = bState[bIdx].colorCycle;
        wr = BWALL_COLORS[c][0]; wg = BWALL_COLORS[c][1]; wb = BWALL_COLORS[c][2];
    }
    setMat(wr,wg,wb);
    glPushMatrix(); glTranslatef(cx,h*0.5f,cz); drawBox(w,h,d); glPopMatrix();
    setMat(rr,rg,rb);
    glPushMatrix(); glTranslatef(cx,h+0.04f,cz); drawBox(w+0.05f,0.08f,d+0.05f); glPopMatrix();
}

// Helper: push a destruction scale transform around a building base
// Returns false if building is fully destroyed (skip draw).
static bool pushDestroyScale(int bIdx, float cx, float cz)
{
    if (bIdx < 0 || bIdx >= NUM_BUILDINGS) return true;
    if (bState[bIdx].destroyProgress >= 0.99f) return false;  // fully gone

    float hs = 1.0f - bState[bIdx].destroyProgress;  // 1→0
    // Scale in Y around the building base (translate to base, scale, translate back)
    glPushMatrix();
    glTranslatef(cx, 0.0f, cz);
    glScalef(1.0f, hs, 1.0f);
    glTranslatef(-cx, 0.0f, -cz);
    return true;
}
static void popDestroyScale(int bIdx)
{
    if (bIdx >= 0 && bIdx < NUM_BUILDINGS && bState[bIdx].destroyProgress < 0.99f)
        glPopMatrix();
}

void drawHouses()
{
    // ── Left house (bIdx = 0) ──────────────────────────────
    glPushMatrix();
    glTranslatef(0.8f, 0.0f, -0.2f);  

    // --- ADD THIS TO ROTATE THE BUILDING IN PLACE ---
    float angle = 180.0f; // Change this to your desired rotation angle
    glTranslatef(-2.3f, 0.0f, -1.5f);   // 1. Move pivot point to the building's center
    glRotatef(angle, 0.0f, 1.0f, 0.0f); // 2. Rotate around the Y (up) axis
    glTranslatef(2.3f, 0.0f, 1.5f);     // 3. Move the world back
    // ------------------------------------------------

    if (pushDestroyScale(0, -2.3f, -1.5f)) {
        drawBuilding(-2.3f,-1.5f, 1.0f,1.0f,0.8f, 0.88f,0.89f,0.90f, 0.10f,0.25f,0.55f, 0);

        setMat(0.10f,0.20f,0.55f);
        glPushMatrix(); glTranslatef(-2.3f,0.8f,-1.5f); glRotatef(-90,1,0,0); glutSolidCone(0.6f,0.5f,4,1); glPopMatrix();

        float winZ=-1.01f;
        setWindowMat(0.20f,0.9f,1.0f);

        glPushMatrix(); glTranslatef(-2.6f,0.25f,winZ); drawBox(0.2f,0.15f,0.04f); glPopMatrix();
        glPushMatrix(); glTranslatef(-2.0f,0.25f,winZ); drawBox(0.2f,0.15f,0.04f); glPopMatrix();
        glPushMatrix(); glTranslatef(-2.6f,0.55f,winZ); drawBox(0.2f,0.15f,0.04f); glPopMatrix();
        glPushMatrix(); glTranslatef(-2.3f,0.55f,winZ); drawBox(0.2f,0.15f,0.04f); glPopMatrix();
        glPushMatrix(); glTranslatef(-2.0f,0.55f,winZ); drawBox(0.2f,0.15f,0.04f); glPopMatrix();

        setMat(0.6f,0.6f,0.6f);
        glPushMatrix(); glTranslatef(-2.3f,0.15f,winZ); drawBox(0.2f,0.30f,0.04f); glPopMatrix();

        popDestroyScale(0);
    }

    glPopMatrix();

    // ── Right house (bIdx = 1) ─────────────────────────────
    if (pushDestroyScale(1, 0.5f, -1.5f)) {
        
        glPushMatrix();
        // 1. Move pivot point to the building's center
        glTranslatef(0.5f, 0.0f, -1.5f);
        // 2. Rotate 180 degrees around the Y (up) axis
        glRotatef(180.0f, 0.0f, 1.0f, 0.0f);
        // 3. Move the world back
        glTranslatef(-0.3f, 0.0f, 1.6f);

        drawBuilding(0.5f,-1.5f, 0.8f,0.8f,0.9f, 0.87f,0.78f,0.72f, 0.55f,0.18f,0.18f, 1);
        
        setMat(0.55f,0.18f,0.18f);
        glPushMatrix(); glTranslatef(0.5f,0.9f,-1.5f); glRotatef(-90,1,0,0); glutSolidCone(0.5f,0.45f,4,1); glPopMatrix();
        
        float rwinZ=-1.11f;
        setWindowMat(0.7f,0.9f,1.0f);
        glPushMatrix(); glTranslatef(0.3f,0.35f,rwinZ); drawBox(0.18f,0.15f,0.04f); glPopMatrix();
        glPushMatrix(); glTranslatef(0.7f,0.35f,rwinZ); drawBox(0.18f,0.15f,0.04f); glPopMatrix();
        glPushMatrix(); glTranslatef(0.5f,0.60f,rwinZ); drawBox(0.18f,0.15f,0.04f); glPopMatrix();
        
        setWindowMat(0.38f,0.60f,0.90f);
        glPushMatrix(); glTranslatef(0.5f,0.15f,rwinZ); drawBox(0.18f,0.30f,0.04f); glPopMatrix();
        
        glPopMatrix(); // End rotation group

        popDestroyScale(1);
    }
    
}
void drawTable(float tx, float ty, float tz)
{
    glPushMatrix();
    glTranslatef(tx, ty, tz);

    // 1. Table Top (Wooden brown)
    setMat(0.65f, 0.35f, 0.15f); 
    glPushMatrix(); 
    glTranslatef(0.0f, 0.20f, 0.0f); // Lift top off the ground
    drawBox(0.25f, 0.02f, 0.25f);    // Width, thickness, depth
    glPopMatrix();

    // 2. Table Leg (Dark center pillar)
    setMat(0.2f, 0.2f, 0.2f); 
    glPushMatrix(); 
    glTranslatef(0.0f, 0.10f, 0.0f); // Halfway up to the top
    drawBox(0.04f, 0.20f, 0.04f); 
    glPopMatrix();

    glPopMatrix();
}

void drawShops()
{
// ── BFC shop (bIdx = 2) ────────────────────────────────
    if (pushDestroyScale(2, -3.3f, -1.5f)) {
        
        glPushMatrix();
        // 1. Move pivot point to the building's center
        glTranslatef(-3.3f, 0.0f, -1.5f);
        // 2. Rotate 180 degrees around the Y (up) axis
        glRotatef(180.0f, 0.0f, 1.0f, 0.0f);
        // 3. Move the world back
        glTranslatef(3.3f, 0.0f, 1.5f);

        drawBuilding(-3.3f,-1.5f, 0.8f,0.6f,0.5f, 1.0f,1.0f,1.0f, 0.50f,0.0f,0.0f, 2);
        
        setMat(1.0f,0.0f,0.0f);
        glPushMatrix(); glTranslatef(-3.3f,0.50f,-1.2f); drawBox(0.85f,0.06f,0.3f); glPopMatrix();
        
        setWindowMat(0.0f,0.9f,0.9f);
        glPushMatrix(); glTranslatef(-3.38f,0.15f,-1.21f); drawBox(0.14f,0.30f,0.04f); glPopMatrix();
        
        glPopMatrix(); // End rotation group
        
        popDestroyScale(2);
    }

    // ── Candy shop (bIdx = 3) ──────────────────────────────

    glPushMatrix();
    glTranslatef(1.0f, 0.0f, 3.0f); // shift from ~-1.5 → +5.0
    
    if (pushDestroyScale(3, -1.0f, -1.5f)) {
        drawBuilding(-1.0f,-1.5f, 1.1f,0.6f,0.6f, 1.0f,0.80f,0.40f, 0.70f,0.35f,0.0f, 3);

        setMat(0.90f,0.45f,0.0f);
        glPushMatrix(); glTranslatef(-1.0f,0.60f,-1.21f); drawBox(1.2f,0.06f,0.3f); glPopMatrix();

        setMat(2.0f,0.0f,0.0f);
        glPushMatrix(); glTranslatef(-1.0f,0.22f,-1.21f); drawBox(1.0f,0.08f,0.04f); glPopMatrix();

        float prodCol[5][3]={{1,0,0},{1,0.8f,0.4f},{0.8f,0,0.4f},{1,0.8f,0},{0.2f,0.6f,1}};
        for (int i=0;i<5;i++) {
            setMat(prodCol[i][0],prodCol[i][1],prodCol[i][2]);
            glPushMatrix(); glTranslatef(-1.4f+i*0.2f,0.30f,-1.21f); drawBox(0.08f,0.12f,0.04f); glPopMatrix();
        }
        drawTable(-1.2f, 0.0f, -1.5f); // Left table
        drawTable(-0.8f, 0.0f, -1.5f);
        glPopMatrix();

        popDestroyScale(3);
    }

    glPopMatrix();
}

void drawFactory(float tx, float ty, float tz)
{
    glPushMatrix();
    glTranslatef(tx, ty, tz);   // ← Controls full factory position

    float cx3[3]={3.0f,3.3f,3.7f};

    if (pushDestroyScale(4, 3.3f, -0.8f)) {

        for (int i=0;i<3;i++)
            drawBuilding(cx3[i],-0.8f, 0.38f,0.6f,0.85f,
                         0.86f,0.67f,0.62f,
                         0.75f,0.46f,0.49f, 4);

        setMat(0.50f,0.50f,0.50f);

        for (int i=0;i<3;i++) {
            glPushMatrix();
            glTranslatef(cx3[i],0.85f,-1.0f);
            drawCylinder(0.06f,0.5f);
            glPopMatrix();
        }

        float sy=1.35f+position_s*0.05f;

        setMat(0.85f,0.85f,0.85f);

        glPushMatrix(); glTranslatef(cx3[0],sy,      -1.0f); glutSolidSphere(0.08f,8,8); glPopMatrix();
        glPushMatrix(); glTranslatef(cx3[0],sy+0.12f,-0.95f); glutSolidSphere(0.07f,8,8); glPopMatrix();
        glPushMatrix(); glTranslatef(cx3[0],sy+0.22f,-1.02f); glutSolidSphere(0.06f,8,8); glPopMatrix();

        popDestroyScale(4);
    }

    glPopMatrix();   // ← restore
}
void drawWindmill(float tx, float ty, float tz)
{
    glPushMatrix();
    glTranslatef(tx, ty, tz);   // ← full control over X, Y, Z

    float wx = 3.2f, wz = -0.5f;

    if (pushDestroyScale(5, wx, wz)) {

        setMat(1.0f,0.40f,0.40f);
        glPushMatrix(); glTranslatef(wx,0.04f,wz); drawBox(0.3f,0.08f,0.3f); glPopMatrix();

        setMat(1.0f,1.0f,1.0f);
        glPushMatrix(); glTranslatef(wx,0.45f,wz); drawBox(0.22f,0.8f,0.22f); glPopMatrix();

        setMat(0.20f,0.60f,1.0f);
        glPushMatrix(); glTranslatef(wx,0.85f,wz); glRotatef(-90,1,0,0); glutSolidCone(0.18f,0.28f,8,1); glPopMatrix();

        setMat(0.20f,0.60f,1.0f);
        glPushMatrix(); glTranslatef(wx,0.15f,wz-0.12f); drawBox(0.08f,0.22f,0.04f); glPopMatrix();

        glPushMatrix();
        glTranslatef(wx,0.90f,wz-0.12f);
        glRotatef(frameNumber*(180.0f/500.0f),0,0,1);

        setMat(1.0f,0.20f,0.20f);
        glutSolidSphere(0.04f,8,8);

        float bladeLen=0.30f;
        float ang[4]={0,90,180,270};

        for (int i=0;i<4;i++) {
            glPushMatrix();
            glRotatef(ang[i],0,0,1);
            glTranslatef(bladeLen*0.5f,0.01f,0.0f);

            setMat(0.80f,0.90f,1.0f);
            drawBox(bladeLen,0.04f,0.02f);

            setMat(1.0f,0.20f,0.20f);
            glTranslatef(bladeLen*0.15f,0.02f,0.0f);
            drawBox(bladeLen*0.7f,0.05f,0.02f);

            glPopMatrix();
        }

        glPopMatrix();
        popDestroyScale(5);
    }

    glPopMatrix();   // ← restore matrix
}

void drawTree(float cx, float cz, float trunkH, float crownR, bool pine)
{

    setMat(0.60f,0.20f,0.20f);
    glPushMatrix(); glTranslatef(cx,trunkH*0.5f,cz); drawCylinder(0.04f,trunkH); glPopMatrix();
    if (pine) {
        setMat(0.0f,0.60f,0.20f);
        glPushMatrix(); glTranslatef(cx,trunkH,cz); glRotatef(-90,1,0,0); glutSolidCone(crownR,crownR*2.2f,10,1); glPopMatrix();
    } else {
        setMat(0.0f,0.60f,0.20f);
        glPushMatrix(); glTranslatef(cx-0.05f,trunkH+crownR*0.8f,cz); glutSolidSphere(crownR,10,8); glPopMatrix();
        glPushMatrix(); glTranslatef(cx,trunkH+crownR,cz);             glutSolidSphere(crownR,10,8); glPopMatrix();
        glPushMatrix(); glTranslatef(cx+0.05f,trunkH+crownR*0.8f,cz); glutSolidSphere(crownR,10,8); glPopMatrix();
        setMat(1.0f,0.0f,0.0f);

        glPushMatrix(); glTranslatef(cx-0.06f,trunkH+crownR*1.4f,cz); glutSolidSphere(0.025f,6,6); glPopMatrix();
        glPushMatrix(); glTranslatef(cx+0.07f,trunkH+crownR*1.1f,cz); glutSolidSphere(0.025f,6,6); glPopMatrix();
    }
}
void drawTrees()
{
    glPushMatrix();
    glTranslatef(0.0f, -0.25f, 0.0f); 
    drawTree(-3.80f,-2.3f,0.50f,0.25f,false);
    drawTree(-2.4f,-2.3f,0.35f,0.16f,false);
    drawTree( 3.1f,-2.3f,0.55f,0.20f,true);  
    drawTree(-0.4f,-2.3f,0.65f,0.20f,true);
    drawTree( 3.8f,-2.3f,0.40f,0.22f,false);
    glPopMatrix();
    
}

void drawFence()
{
    setMat(0.80f,0.0f,0.0f);
    glPushMatrix(); glTranslatef(-1.2f,0.14f,-1.0f); drawBox(4.4f,0.03f,0.03f); glPopMatrix();
    glPushMatrix(); glTranslatef(-1.2f,0.06f,-1.0f); drawBox(4.4f,0.03f,0.03f); glPopMatrix();
    glPushMatrix(); glTranslatef( 3.4f,0.14f,-1.0f); drawBox(1.2f,0.03f,0.03f); glPopMatrix();
    glPushMatrix(); glTranslatef( 3.4f,0.06f,-1.0f); drawBox(1.2f,0.03f,0.03f); glPopMatrix();
    for (int i=0;i<16;i++) { glPushMatrix(); glTranslatef(-3.3f+i*0.28f,0.10f,-1.0f); drawBox(0.03f,0.20f,0.03f); glPopMatrix(); }
    for (int i=0;i<5;i++)  { glPushMatrix(); glTranslatef( 2.9f+i*0.28f,0.10f,-1.0f); drawBox(0.03f,0.20f,0.03f); glPopMatrix(); }
}

// ─────────────────────────────────────────────────────────────
//  § 2  LIVING INFRASTRUCTURE – Traffic Lights (unchanged)
// ─────────────────────────────────────────────────────────────
void drawTrafficLight(float px, float py, float pz, bool redOn, bool yellowOn, bool greenOn)
{
    setMat(0.50f,0.0f,0.0f);
    glPushMatrix(); glTranslatef(px,py*0.5f,pz); drawCylinder(0.03f,py); glPopMatrix();
    setMat(0.80f,0.48f,0.0f);
    glPushMatrix(); glTranslatef(px,py+0.12f,pz); drawBox(0.10f,0.28f,0.10f); glPopMatrix();

    // Red bulb – glows solid red, otherwise a dim unlit lens
    setMat(redOn?1.0f:0.40f, redOn?0.05f:0.0f, redOn?0.05f:0.0f);
    glPushMatrix(); glTranslatef(px,py+0.24f,pz-0.055f); glutSolidSphere(0.035f,8,8); glPopMatrix();

    // Amber bulb – bright and flashes brighter during the caution phase
    setMat(yellowOn?1.0f:0.35f, yellowOn?0.75f:0.28f, 0.0f);
    glPushMatrix(); glTranslatef(px,py+0.12f,pz-0.055f); glutSolidSphere(0.035f,8,8); glPopMatrix();

    // Green bulb
    setMat(0.0f, greenOn?1.0f:0.20f, greenOn?0.15f:0.0f);
    glPushMatrix(); glTranslatef(px,py+0.00f,pz-0.055f); glutSolidSphere(0.035f,8,8); glPopMatrix();

    // Emissive glow halo on whichever bulb is lit (cheap "bloom" trick that
    // reads as a real glowing signal lamp instead of a flat coloured sphere)
    if (redOn || yellowOn || greenOn) {
        float gy = redOn ? (py+0.24f) : (yellowOn ? (py+0.12f) : (py+0.00f));
        float gr = redOn ? 1.0f : (yellowOn ? 1.0f : 0.15f);
        float gg = redOn ? 0.1f : (yellowOn ? 0.75f : 1.0f);
        float gb = redOn ? 0.1f : 0.0f;
        glDisable(GL_LIGHTING); glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(gr,gg,gb,0.30f);
        glPushMatrix(); glTranslatef(px,gy,pz-0.055f); glutSolidSphere(0.065f,10,10); glPopMatrix();
        glDisable(GL_BLEND); glEnable(GL_LIGHTING);
    }
}

// Derives the live bulb state for the horizontal-road and vertical-road
// signal heads from the cnt / signalYellow / signalAllRed state machine.
void getSignalStates(bool &hzRed, bool &hzYellow, bool &hzGreen,
                      bool &vtRed, bool &vtYellow, bool &vtGreen)
{
    bool hzIsActiveSide = (cnt == 0);   // horizontal owns the green when cnt==0
    if (signalAllRed) {
        hzRed = true; hzYellow = false; hzGreen = false;
        vtRed = true; vtYellow = false; vtGreen = false;
        return;
    }
    hzGreen  = hzIsActiveSide  && !signalYellow;
    hzYellow = hzIsActiveSide  &&  signalYellow;
    hzRed    = !hzIsActiveSide;
    vtGreen  = !hzIsActiveSide && !signalYellow;
    vtYellow = !hzIsActiveSide &&  signalYellow;
    vtRed    = hzIsActiveSide;
}

void drawTrafficLights()
{
    bool hzRed,hzYellow,hzGreen, vtRed,vtYellow,vtGreen;
    getSignalStates(hzRed,hzYellow,hzGreen, vtRed,vtYellow,vtGreen);

    glPushMatrix();
        glTranslatef(2.5f, -0.3f, -0.7f);   // move to its position
        glRotatef(270.0f, 0.0f, 1.0f, 0.0f); // rotate 90° around Y-axis
        drawTrafficLight(0.0f, 0.6f, 0.0f,  hzRed,hzYellow,hzGreen); // draw at origin after transform
    glPopMatrix();


    glPushMatrix();
        glTranslatef(1.1f, -0.3f, 0.7f);     // move to its position
        glRotatef(90.0f, 0.0f, 1.0f, 0.0f); // rotate around Y-axis
        drawTrafficLight(0.0f, 0.6f, 0.0f, hzRed,hzYellow,hzGreen);
    glPopMatrix();

    glPushMatrix();
        glTranslatef(2.5f, -0.3f, 0.7f);     // move to its position
        glRotatef(180.0f, 0.0f, 1.0f, 0.0f); // rotate around Y-axis
        drawTrafficLight(0.0f, 0.6f, 0.0f, vtRed,vtYellow,vtGreen);
    glPopMatrix();


    glPushMatrix();
        glTranslatef(1.1f, -0.3f, -0.7f);     // move to its position
        glRotatef(0.0f, 0.0f, 1.0f, 0.0f); // rotate around Y-axis
        drawTrafficLight(0.0f, 0.6f, 0.0f, vtRed,vtYellow,vtGreen);
    glPopMatrix();
    
}
// ─────────────────────────────────────────────────────────────
//  § 2  LIVING INFRASTRUCTURE – Street Lamp + NEW light beam
// ─────────────────────────────────────────────────────────────

void drawLamp(float cx, float cy, float cz, float angle)
{
    glPushMatrix();

    glTranslatef(cx, cy, cz);
    glRotatef(angle, 0.0f, 1.0f, 0.0f); // rotate around Y-axis

    // Pole
    setMat(0.50f,0.50f,0.50f);
    glPushMatrix(); 
    glTranslatef(0.0f, -0.2f, 0.0f); 
    drawCylinder(0.025f, 0.8f); 
    glPopMatrix();

    // Arm
    setMat(0.75f,0.75f,0.75f);
    glPushMatrix(); 
    glTranslatef(0.0f, 0.50f, 0.0f); 
    drawBox(0.02f, 0.02f, 0.22f); 
    glPopMatrix();

    // Lamp head
    bool lampOn = (flag != 0) && !powerOutage;
    if (lampOn) setMat(1.0f,1.0f,0.80f); 
    else setMat(0.75f,0.75f,0.65f);

    glPushMatrix(); 
    glTranslatef(0.0f, 0.50f, -0.10f); 
    drawBox(0.06f, 0.06f, 0.10f); 
    glPopMatrix();

    glPopMatrix();
}

void drawStreetLightBeam(float cx, float cy, float cz, float angle)
{
    if (!isNight || powerOutage) return;

    glPushMatrix();

    glTranslatef(cx, cy, cz);
    glRotatef(angle, 0.0f, 1.0f, 0.0f);

    float bx  = 0.0f;
    float by  = 0.50f;
    float bz  = -0.10f;

    float coneR = 0.55f;
    int   segs  = 16;

    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);

    glColor4f(1.0f, 0.95f, 0.50f, 0.07f);

    glBegin(GL_TRIANGLE_FAN);
        glVertex3f(bx, by, bz);

        for (int i = 0; i <= segs; i++) {
            float angleRad = 2.0f * PI * i / segs;
            glVertex3f(
                bx + coneR * cosf(angleRad),
                0.005f,
                bz + coneR * sinf(angleRad)
            );
        }
    glEnd();

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LIGHTING);

    glPopMatrix();
}

// Draw all lamps + their beams when night

void drawLamps()
{
    struct LP { float x, z, angle; } lamps[] = {
        {-3.4f, 0.9f,  0.0f},
        {-2.2f, 0.9f,  0.0f},
        {-1.0f, 0.9f, 0.0f},
        {0.2f,  0.9f, 0.0f},

        {-3.1f,-0.9f,  180.0f},
        {-1.9f,-0.9f, 180.0f},
        {-0.7f,-0.9f, 180.0f},
        {0.5f, -0.9f, 180.0f},

        {3.5f,  0.9f,  0.0f},
        {3.5f, -0.9f, 180.0f},

        {1.0f, -3.0f,-90.0f},
        {2.6f, -3.0f, 90.0f},

        {1.0f,  2.5f, -90.0f},
        {2.6f,  2.5f,90.0f},
    };

    int nLamps = (int)(sizeof(lamps)/sizeof(lamps[0]));

    for (int i = 0; i < nLamps; i++) {
        drawLamp(lamps[i].x, 0.0f, lamps[i].z, lamps[i].angle);
        drawStreetLightBeam(lamps[i].x, 0.0f, lamps[i].z, lamps[i].angle);
    }
}
// ─────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────
//  RIVER  –  animated multi-wave surface with REAL shading.
//  The previous version perturbed vertex height with a sine wave
//  but still passed a fixed straight-up normal, so lighting never
//  reacted to the ripples - it just looked like a flat blue sheet
//  sliding around. This version computes each vertex's normal
//  analytically from the height field's slope, so the ripples are
//  actually lit: crests catch highlights, troughs sit darker. A
//  darker riverbed underneath plus a touch of transparency add
//  depth, and per-vertex colour (via GL_COLOR_MATERIAL) tints
//  crests toward pale foam and troughs toward deep teal.
//
//  BUGFIX: the water's base height used to be only 0.006 while the
//  wave amplitude could swing +/-0.035 - so on every trough the
//  surface dipped to y < 0, which is BELOW the ground plane (y=0).
//  With depth testing on, the grass then legitimately won the depth
//  test and showed through the "hole". The riverbed quad was drawn
//  at y=-0.02 - also below the ground - so it could never be seen
//  either, it was just dead geometry. Fix: raise the water's base
//  well above the ground, shrink the wave amplitude so the lowest
//  possible trough still stays comfortably positive, and move the
//  riverbed to sit just above the grass (not below it) so it's
//  actually visible as the "bottom" of the water.
// ─────────────────────────────────────────────────────────────
static const float RIVER_X0=-4.0f, RIVER_X1=0.8f, RIVER_Z0=-4.0f, RIVER_Z1=-2.5f;
static const float RIVER_BASE_Y   = 0.045f;   // comfortably above ground (y=0) and riverbed
static const float RIVER_BED_Y    = 0.006f;   // just above the grass, so it's actually visible
static const float RIVER_AMPLITUDE = 0.022f;  // max +/- swing of the three waves combined

// Sum of a few sine waves at different frequencies/directions/speeds -
// a single sine looks like a metronome; three overlapping ones look
// like actual chop. Coefficients sum to RIVER_AMPLITUDE so the surface
// can never swing far enough to reach the riverbed underneath it.
static inline float riverHeight(float x, float z, float t)
{
    return 0.011f*sinf(x*2.6f        + t*1.0f)
         + 0.007f*sinf(x*4.4f - z*3.1f + t*1.7f)
         + 0.004f*sinf(z*5.0f        + t*0.6f);
}
// Analytic slope of riverHeight, used to build a proper lighting normal
// instead of leaving it pinned straight up.
static inline void riverSlope(float x, float z, float t, float &dhdx, float &dhdz)
{
    dhdx = 0.011f*2.6f*cosf(x*2.6f + t*1.0f)
         + 0.007f*4.4f*cosf(x*4.4f - z*3.1f + t*1.7f);
    dhdz = 0.007f*(-3.1f)*cosf(x*4.4f - z*3.1f + t*1.7f)
         + 0.004f*5.0f*cosf(z*5.0f + t*0.6f);
}

void drawRiver()
{
    const int NX = 34, NZ = 10;
    const float t = riverWaveOffset;

    // Riverbed - a plain darker quad sitting just above the grass (not
    // below it - see bugfix note above) so the water reads as a
    // translucent volume with visible depth instead of a coloured lid.
    setMat(0.06f,0.22f,0.24f);
    flatQuad(RIVER_X0,RIVER_Z0,RIVER_X1,RIVER_Z1,RIVER_BED_Y);

    // Water surface: lit via real normals, with GL_COLOR_MATERIAL driving
    // per-vertex diffuse colour so crests/troughs tint differently.
    setMat(0.10f,0.45f,0.55f, 0.88f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_DIFFUSE);

    for (int j = 0; j < NZ; j++) {
        float za = RIVER_Z0 + (RIVER_Z1-RIVER_Z0) * (float)j     / NZ;
        float zb = RIVER_Z0 + (RIVER_Z1-RIVER_Z0) * (float)(j+1) / NZ;
        glBegin(GL_TRIANGLE_STRIP);
        for (int i = 0; i <= NX; i++) {
            float xt = (float)i / NX;
            float x  = RIVER_X0 + (RIVER_X1-RIVER_X0)*xt;

            for (int row = 0; row < 2; row++) {
                float z = (row == 0) ? za : zb;
                float h = riverHeight(x, z, t);
                float dhdx, dhdz; riverSlope(x, z, t, dhdx, dhdz);
                // Normal from the surface slope: steeper slope -> normal
                // leans further from straight up -> real per-pixel-ish shading.
                float nx = -dhdx, ny = 1.0f, nz = -dhdz;
                float len = sqrtf(nx*nx+ny*ny+nz*nz);
                glNormal3f(nx/len, ny/len, nz/len);

                // Crest -> pale/foamy, trough -> deep teal
                float crest = clampF((h + RIVER_AMPLITUDE) / (2.0f*RIVER_AMPLITUDE), 0.0f, 1.0f);
                glColor4f(lerpF(0.06f,0.55f,crest), lerpF(0.30f,0.75f,crest),
                          lerpF(0.38f,0.80f,crest), 0.88f);

                glVertex3f(x, RIVER_BASE_Y + h, z);
            }
        }
        glEnd();
    }

    glDisable(GL_COLOR_MATERIAL);
    glDisable(GL_BLEND);

    // Sparkle glints - a handful of small bright points scattered on the
    // surface that flicker in and out, like sunlight catching ripple
    // facets. Cheap, but reads far more like water than a single scrolling
    // highlight bar.
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glPointSize(2.2f);
    glBegin(GL_POINTS);
    for (int k = 0; k < 24; k++) {
        float fx = RIVER_X0 + (RIVER_X1-RIVER_X0) * fmodf(k*0.6180339f + t*0.02f, 1.0f);
        float fz = RIVER_Z0 + (RIVER_Z1-RIVER_Z0) * fmodf(k*0.3819660f, 1.0f);
        float sparkle = 0.5f + 0.5f*sinf(t*3.0f + k*2.1f);
        if (sparkle < 0.6f) continue;   // most are off at any instant - flicker
        float h = riverHeight(fx, fz, t);
        glColor4f(1.0f,1.0f,1.0f, (sparkle-0.6f)*2.2f);
        glVertex3f(fx, RIVER_BASE_Y + 0.004f + h, fz);
    }
    glEnd();
    glPointSize(1.0f);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

// ─────────────────────────────────────────────────────────────
//  BOATS  –  fake shadows added
// ─────────────────────────────────────────────────────────────
void drawBoat(float tx, float tz,
              GLfloat hr, GLfloat hg, GLfloat hb,
              GLfloat br, GLfloat bg, GLfloat bb)
{
    // Fake shadow first
    drawFakeShadow(tx, tz, 0.55f, 0.22f);

    glPushMatrix(); glTranslatef(tx,0.03f,tz);
    setMat(br,bg,bb);
    glPushMatrix(); glTranslatef(0,0.03f,0); drawBox(0.5f,0.06f,0.18f); glPopMatrix();
    setMat(hr,hg,hb);
    glPushMatrix(); glTranslatef(0.05f,0.09f,0); drawBox(0.22f,0.08f,0.14f); glPopMatrix();
    setMat(1.0f,1.0f,1.0f);
    glPushMatrix(); glTranslatef(0,0.07f,0); drawCylinder(0.012f,0.22f); glPopMatrix();
    glPopMatrix();
}
void drawBoats()
{
    drawBoat(position_b1*2.0f,-3.5f, 1.0f,0.55f,0.10f, 0.40f,0.20f,0.0f);
    drawBoat(position_b2*2.0f,-3.0f, 0.90f,0.90f,0.0f, 0.0f,0.0f,0.0f);
}

// ─────────────────────────────────────────────────────────────
//  CARS  –  fake shadows + traffic sensor braking
// ─────────────────────────────────────────────────────────────
// Realistic wheel: a short dark-rubber cylinder (tyre) whose flat faces
// point sideways (the natural gluCylinder axis), capped with a light
// hubcap disc on each face - reads as an actual tyre+rim instead of a
// grey ball sitting under the car.
// spinDeg rotates the wheel about its own axle (local Z, the direction the
// tyre's thickness runs) so it visibly rolls as the car moves. A small dark
// "bolt" marker off-centre on each hubcap face is what actually makes the
// rotation readable — a plain grey disc looks identical at every angle.
static void drawWheel(float radius, float width, float spinDeg = 0.0f)
{
    glPushMatrix();
    glRotatef(spinDeg, 0.0f, 0.0f, 1.0f);

    GLUquadric *q = gluNewQuadric(); gluQuadricNormals(q,GLU_SMOOTH);
    setMat(0.07f,0.07f,0.07f);
    glPushMatrix();
        glTranslatef(0,0,-width*0.5f);
        gluCylinder(q, radius, radius, width, 14, 1);
        gluDisk(q, 0, radius, 14, 1);
        glTranslatef(0,0,width);
        gluDisk(q, 0, radius, 14, 1);
    glPopMatrix();
    setMat(0.62f,0.63f,0.66f);
    glPushMatrix(); glTranslatef(0,0,-width*0.5f-0.003f); gluDisk(q, 0, radius*0.55f, 12, 1); glPopMatrix();
    glPushMatrix(); glTranslatef(0,0, width*0.5f+0.003f); gluDisk(q, 0, radius*0.55f, 12, 1); glPopMatrix();
    gluDeleteQuadric(q);

    setMat(0.05f,0.05f,0.05f);
    glPushMatrix(); glTranslatef(radius*0.4f, radius*0.4f, -width*0.5f-0.004f); drawBox(0.012f,0.012f,0.006f); glPopMatrix();
    glPushMatrix(); glTranslatef(radius*0.4f, radius*0.4f,  width*0.5f+0.004f); drawBox(0.012f,0.012f,0.006f); glPopMatrix();

    glPopMatrix();
}

void drawCar3D(float bodyR, float bodyG, float bodyB, bool headlights, bool braking=false, float wheelSpinDeg=0.0f)
{
    setMat(bodyR,bodyG,bodyB);
    glPushMatrix(); glTranslatef(0,0.06f,0); drawBox(0.40f,0.12f,0.22f); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.04f,0.165f,0); drawBox(0.24f,0.10f,0.19f); glPopMatrix();
    setMat(0.10f,0.10f,0.15f);
    glPushMatrix(); glTranslatef( 0.08f,0.165f,0); drawBox(0.06f,0.09f,0.18f); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.16f,0.165f,0); drawBox(0.06f,0.09f,0.18f); glPopMatrix();

    // Wheels - cylindrical tyres with hubcaps, correctly oriented so the
    // axle runs left-right through the car instead of floating spheres.
    // wheelSpinDeg rolls them in sync with the car's actual travel speed.
    float wx[4]={0.13f,0.13f,-0.13f,-0.13f};
    float wz_[4]={0.12f,-0.12f,0.12f,-0.12f};
    for (int i=0;i<4;i++) {
        glPushMatrix(); glTranslatef(wx[i],0.065f,wz_[i]); drawWheel(0.065f,0.055f,wheelSpinDeg); glPopMatrix();
    }

    if (headlights && isNight && !powerOutage) {
        setMat(1.0f,1.0f,0.80f);
        glPushMatrix(); glTranslatef(0.21f,0.07f, 0.07f); glutSolidSphere(0.025f,6,6); glPopMatrix();
        glPushMatrix(); glTranslatef(0.21f,0.07f,-0.07f); glutSolidSphere(0.025f,6,6); glPopMatrix();
        glDisable(GL_LIGHTING); glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(1.0f,1.0f,0.7f,0.18f);
        glBegin(GL_TRIANGLES);
            glVertex3f(0.21f,0.07f, 0.07f); glVertex3f(0.90f,0.00f, 0.30f); glVertex3f(0.90f,0.14f, 0.07f);
            glVertex3f(0.21f,0.07f,-0.07f); glVertex3f(0.90f,0.00f,-0.30f); glVertex3f(0.90f,0.14f,-0.07f);
        glEnd();
        glDisable(GL_BLEND); glEnable(GL_LIGHTING);
    }

    // Tail-lights: dim red glow at all times (visible detail even by day),
    // flare bright red when the traffic sensor is actively braking the
    // car - a small but very legible cue for "why did that car slow down".
    {
        float baseR = braking ? 1.00f : (isNight ? 0.55f : 0.30f);
        setMat(baseR, 0.03f, 0.03f);
        glPushMatrix(); glTranslatef(-0.205f,0.09f, 0.075f); drawBox(0.02f,0.035f,0.05f); glPopMatrix();
        glPushMatrix(); glTranslatef(-0.205f,0.09f,-0.075f); drawBox(0.02f,0.035f,0.05f); glPopMatrix();
        if (braking) {
            glDisable(GL_LIGHTING); glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(1.0f,0.12f,0.08f,0.25f);
            glPushMatrix(); glTranslatef(-0.21f,0.09f,0.0f); glutSolidSphere(0.06f,8,8); glPopMatrix();
            glDisable(GL_BLEND); glEnable(GL_LIGHTING);
        }
    }
}

// § 2 – applyTrafficSensor: distance-based gradual braking
// Called each frame from display().  A car begins braking the moment
// its road is not fully green (i.e. amber or red), the same way a real
// driver lifts off the accelerator as soon as the light turns amber
// rather than waiting for red. All four approaches now brake the same
// way, instead of two smoothly slowing and two snapping to a hard stop.
bool carBraking[4] = { false, false, false, false };  // for tail-light flare

void applyTrafficSensor()
{
    bool hzRed,hzYellow,hzGreen, vtRed,vtYellow,vtGreen;
    getSignalStates(hzRed,hzYellow,hzGreen, vtRed,vtYellow,vtGreen);
    bool hzMustStop = !hzGreen;   // amber or red on the horizontal road
    bool vtMustStop = !vtGreen;   // amber or red on the vertical road

    // Braking now eases the speed value toward its distance-based target
    // (speed += (target-speed)*rate) each tick instead of snapping to it
    // directly - the deceleration itself reads as smooth and gradual
    // rather than a robotic step change every frame.
    const float BRAKE_EASE = 0.18f;

    // ── Car1: moving right (+x),  stops at STOP_C1 ────────
    carBraking[0] = false;
    if (hzMustStop) {
        float dist1 = STOP_C1 - position_c1;   // positive when car is before stop line
        if (dist1 > 0.0f && dist1 < SENSOR_RANGE) {
            float brakeFactor = dist1 / SENSOR_RANGE;  // 1.0 (far) → 0.0 (at line)
            speed_c1 += (0.01f*brakeFactor - speed_c1) * BRAKE_EASE;
            carBraking[0] = true;
        }
        if (dist1 <= 0.0f) { speed_c1 += (0.0f - speed_c1) * BRAKE_EASE; if (speed_c1<0.0004f) speed_c1=0.0f; position_c1 = STOP_C1; }
    }

    // ── Car2: moving left  (-x),  stops at STOP_C2 ────────
    carBraking[1] = false;
    if (hzMustStop) {
        float dist2 = position_c2 - STOP_C2;
        if (dist2 > 0.0f && dist2 < SENSOR_RANGE) {
            speed_c2 += (0.01f*(dist2/SENSOR_RANGE) - speed_c2) * BRAKE_EASE;
            carBraking[1] = true;
        }
        if (dist2 <= 0.0f) { speed_c2 += (0.0f - speed_c2) * BRAKE_EASE; if (speed_c2<0.0004f) speed_c2=0.0f; position_c2 = STOP_C2; }
    }

    // ── Car3: moving down (+z), stops at STOP_C3 ──────────
    carBraking[2] = false;
    if (vtMustStop) {
        float dist3 = STOP_C3 - position_c3;
        if (dist3 > 0.0f && dist3 < SENSOR_RANGE) {
            speed_c3 += (0.01f*(dist3/SENSOR_RANGE) - speed_c3) * BRAKE_EASE;
            carBraking[2] = true;
        }
        if (dist3 <= 0.0f) { speed_c3 += (0.0f - speed_c3) * BRAKE_EASE; if (speed_c3<0.0004f) speed_c3=0.0f; position_c3 = STOP_C3; }
    }

    // ── Car4: moving up (-z), stops at STOP_C4 ────────────
    carBraking[3] = false;
    if (vtMustStop) {
        float dist4 = position_c4 - STOP_C4;
        if (dist4 > 0.0f && dist4 < SENSOR_RANGE) {
            speed_c4 += (0.01f*(dist4/SENSOR_RANGE) - speed_c4) * BRAKE_EASE;
            carBraking[3] = true;
        }
        if (dist4 <= 0.0f) { speed_c4 += (0.0f - speed_c4) * BRAKE_EASE; if (speed_c4<0.0004f) speed_c4=0.0f; position_c4 = STOP_C4; }
    }
}

// ── Adaptive signal core: is a car currently near/using this stop line? ──
// dist follows the same sign convention as applyTrafficSensor(): positive
// = still approaching, negative = just past the line but still inside
// the intersection footprint.
static bool nearStopLine(float dist)
{
    return dist > -DETECT_BEHIND && dist < DETECT_AHEAD;
}

// Reports whether each road currently has a car near/crossing the
// intersection - this is the "sensor" an actuated real-world signal
// would read from an inductive loop embedded in the road.
void computeRoadActivity(bool &hzNear, bool &vtNear)
{
    float dist1 = STOP_C1 - position_c1;
    float dist2 = position_c2 - STOP_C2;
    float dist3 = STOP_C3 - position_c3;
    float dist4 = position_c4 - STOP_C4;
    hzNear = nearStopLine(dist1) || nearStopLine(dist2);
    vtNear = nearStopLine(dist3) || nearStopLine(dist4);
}

// ── Auto-cycling signal timer: green → amber → all-red → repeat ──
// dtSeconds is the real elapsed time since the last tick (the timer
// below fires every 100ms, so dtSeconds is fixed at 0.1s per call).
void advanceSignal(float dtSeconds)
{
    if (!signalAuto) return;
    signalClock += dtSeconds;

    if (signalAllRed) {
        if (signalClock >= SIGNAL_ALLRED_TIME) {
            signalClock = 0.0f;
            signalAllRed = false;
            cnt = (cnt == 0) ? 1 : 0;          // flip which road owns green
            speed_c1 = 0.01f; speed_c2 = 0.01f; // release the new green road
        }
        return;
    }
    if (signalYellow) {
        if (signalClock >= SIGNAL_YELLOW_TIME) {
            signalClock = 0.0f;
            signalYellow = false;
            signalAllRed = true;                // brief all-red clearance
        }
        return;
    }

    // Solid green: decide adaptively whether to keep holding it.
    bool hzNear, vtNear;
    computeRoadActivity(hzNear, vtNear);
    bool hzIsGreen    = (cnt == 0);
    bool ownNear      = hzIsGreen ? hzNear : vtNear;  // still using this green
    bool oppositeNear = hzIsGreen ? vtNear : hzNear;  // someone waiting for it

    if (signalClock >= SIGNAL_MAX_GREEN) {
        // Fairness cap: never let one direction hog green indefinitely.
        signalClock = 0.0f; signalYellow = true;
    } else if (signalClock >= SIGNAL_MIN_GREEN && oppositeNear && !ownNear) {
        // Road's empty AND the other side actually has a car waiting -
        // hand the green over now instead of wasting time on it.
        signalClock = 0.0f; signalYellow = true;
    }
    // Otherwise keep the green: either nobody is waiting on the other
    // side (no point switching), or a car is still approaching/crossing
    // on this green road (don't strand it mid-intersection).
}

void drawCars()
{
    // c1 – red car, horizontal right
    float c1x = position_c1*1.7f;
    drawFakeShadow(c1x, 0.25f, 0.45f, 0.26f);
    glPushMatrix(); 
    glTranslatef(c1x,0.0f,0.25f);  
    drawCar3D(0.90f,0.0f,0.0f,true,carBraking[0],wheelSpin1);  
    glPopMatrix();

    // c2 – green car, horizontal left
    float c2x = position_c2*1.7f;
    drawFakeShadow(c2x,-0.25f, 0.45f, 0.26f);
    glPushMatrix(); 
    glTranslatef(c2x,0.0f,-0.25f); 
    glRotatef(180,0,1,0); 
    drawCar3D(0.0f,0.60f,0.10f,true,carBraking[1],wheelSpin2); 
    glPopMatrix();

    // c3 – yellow car, vertical down
    float c3z = position_c3*1.7f;
    drawFakeShadow(1.5f, c3z, 0.26f, 0.45f);
    glPushMatrix(); 
    glTranslatef(1.5f,0.0f,c3z); 
    glRotatef(270,0,1,0); 
    drawCar3D(1.0f,0.80f,0.0f,true,carBraking[2],wheelSpin3); 
    glPopMatrix();

    // c4 – blue car, vertical up
    float c4z = position_c4*1.7f;
    drawFakeShadow(2.1f, c4z, 0.26f, 0.45f);
    glPushMatrix(); 
    glTranslatef(2.1f,0.0,c4z); 
    glRotatef(90,0,1,0); 
    drawCar3D(0.20f,0.40f,1.0f,true,carBraking[3],wheelSpin4); glPopMatrix();
}

// ─────────────────────────────────────────────────────────────
//  RAIN  (original, unchanged)
// ─────────────────────────────────────────────────────────────
void drawRain()
{
    if (r == 0) return;

    // Disable lighting for clean rain color
    glDisable(GL_LIGHTING);

    // Enable blending FIRST
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Set transparent rain color
    glColor4f(0.7f, 0.85f, 1.0f, 0.6f);

    glLineWidth(1.0f);

    float offY = position_rain;
    float offX = position_rain2 * 0.3f;

    glBegin(GL_LINES);

    for (int i = 0; i < 150; i++)
    {
        float x = -4.0f + fmod(i * 1.37f, 8.0f) + offX;
        float z = -4.0f + fmod(i * 2.11f, 8.0f);

        float y = fmod((i * 0.5f + offY * 6.0f), 5.0f);
        float length = 0.25f + fmod(i * 0.3f, 0.15f);

        glVertex3f(x, 3.0f - y, z);
        glVertex3f(x + 0.05f, 3.0f - y - length, z);
    }

    glEnd();

    // Disable blending after drawing
    glDisable(GL_BLEND);

    // Restore lighting
    glEnable(GL_LIGHTING);

    // ---- Animation update (keep it smooth) ----
    position_rain -= 0.02f;     // vertical speed
    position_rain2 += 0.005f;   // slight wind
}

// ═══════════════════════════════════════════════════════════
//  § 1A  SKYBOX GRADIENT
//  Drawn first in display() with depth writes OFF so it sits
//  infinitely far behind all geometry.
//  Uses a full-screen ortho quad with a vertical colour sweep.
// ═══════════════════════════════════════════════════════════
void drawSkyboxGradient()
{
    float zenith[3], horizon[3];
    evalSkyColour(skyPhase, zenith, horizon);

    // Switch to 2-D ortho so the quad always fills the screen
    glMatrixMode(GL_PROJECTION);
    glPushMatrix(); glLoadIdentity(); glOrtho(-1,1,-1,1,-1,1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix(); glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDepthMask(GL_FALSE);

    glBegin(GL_QUADS);
        // Top edge – zenith colour
        glColor3f(zenith[0], zenith[1], zenith[2]);
        glVertex2f(-1.0f,  1.0f);
        glVertex2f( 1.0f,  1.0f);
        // Bottom edge – horizon colour
        glColor3f(horizon[0], horizon[1], horizon[2]);
        glVertex2f( 1.0f, -1.0f);
        glVertex2f(-1.0f, -1.0f);
    glEnd();

    glDepthMask(GL_TRUE);

    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW);  glPopMatrix();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}

// ═══════════════════════════════════════════════════════════
//  § 1A  SMOOTH SKY TRANSITION
//  Sets glClearColor each frame by lerping between sky palettes.
//  Also derives isNight and updates fog colour.
// ═══════════════════════════════════════════════════════════
void smoothSkyTransition()
{
    // Lerp skyPhase towards target (0=day, 1=sunset, 2=night)
    float diff = skyPhaseTarget - skyPhase;
    if (fabsf(diff) > 0.005f) skyPhase += diff * 0.02f;
    else                       skyPhase  = skyPhaseTarget;

    // Wrap for auto-cycle
    if (skyAutoAdvance) {
        skyPhase += 0.0003f;
        if (skyPhase >= 3.0f) skyPhase -= 3.0f;
    }

    // Derive isNight for other systems
    isNight = (skyPhase > 1.3f && skyPhase < 2.7f);

    // Compute horizon colour for glClearColor (lower half of sky)
    float z[3], h[3];
    evalSkyColour(skyPhase, z, h);
    glClearColor(h[0], h[1], h[2], 1.0f);

    // Keep fog colour matching sky horizon
    float fogCol[4] = { h[0], h[1], h[2], 1.0f };
    glFogfv(GL_FOG_COLOR, fogCol);
}

// ═══════════════════════════════════════════════════════════
//  § 1B  VOLUMETRIC FOG  –  transitionFog()
//  Uses GL_FOG_EXP2 with a density that lerps smoothly.
//  Dense when raining (targetFogDensity = 0.12),
//  clears to 0 otherwise.
// ═══════════════════════════════════════════════════════════
void transitionFog()
{
    isRaining = (r > 0);

    // Lerp target density towards goal
    targetFogDensity = isRaining ? 0.12f : 0.0f;
    // Lerp:  currentFogDensity = start + t*(end - start)
    currentFogDensity = lerpF(currentFogDensity, targetFogDensity, 0.025f);

    if (currentFogDensity > 0.001f) {
        glEnable(GL_FOG);
        glFogi(GL_FOG_MODE, GL_EXP2);
        glFogf(GL_FOG_DENSITY, currentFogDensity);
        glFogf(GL_FOG_START,   1.0f);
        glFogf(GL_FOG_END,    30.0f);
        glHint(GL_FOG_HINT, GL_NICEST);
    } else {
        glDisable(GL_FOG);
    }
}

// ═══════════════════════════════════════════════════════════
//  § 1C  PROCEDURAL CLOUDS
//  Each cloud is a glutSolidSphere flattened to 20% height
//  via glScalef(rx, 0.2f, rz), semi-transparent alpha 0.5.
//  cloudOffset scrolls them slowly west→east.
// ═══════════════════════════════════════════════════════════
void drawClouds()
{
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    // Cloud brightness depends on time of day
    float brightness = 1.0f;
    if (isNight) brightness = 0.25f;
    else if (skyPhase > 0.5f && skyPhase < 1.5f) brightness = 0.85f; // sunset tinge

    for (int i = 0; i < NUM_CLOUDS; i++) {
        const CloudDesc& c = CLOUDS[i];
        // Offset scrolls clouds along +X
        float cx = c.x + cloudOffset;
        // Wrap around scene bounds
        if (cx >  6.0f) cx -= 12.0f;
        if (cx < -6.0f) cx +=  12.0f;

        glColor4f(brightness, brightness*0.98f, brightness, 0.50f);
        glPushMatrix();
            glTranslatef(cx, c.y, c.z);
            // Flatten the sphere: full X/Z radius, compressed Y to 20%
            glScalef(c.rx, 0.20f, c.rz);
            glutSolidSphere(1.0, 12, 6);
        glPopMatrix();
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LIGHTING);
}

// ═══════════════════════════════════════════════════════════
//  § 1D  SUN / MOON DISC  +  STARS  (Quick win #1)
//  A glowing billboard-ish sphere that arcs across the sky in
//  sync with skyPhase (0=day .. 3=back to day), plus a field of
//  twinkling GL_POINTS stars once isNight is true.
// ═══════════════════════════════════════════════════════════
void drawSunMoon()
{
    // Sweep a half-circle arc across the sky as skyPhase advances through
    // its full day→sunset→night→(dawn) cycle.
    float arc    = (skyPhase / 3.0f) * PI;      // 0..PI over the full cycle
    float radius = 14.0f;
    float elev   = sinf(arc);
    float horiz  = cosf(arc);
    float sx = horiz * radius;
    float sy = 3.0f + elev * 9.0f;
    float sz = -6.0f - horiz*2.0f;

    glDisable(GL_LIGHTING);
    glDepthMask(GL_FALSE);
    glPushMatrix();
    glTranslatef(sx, sy, sz);

    if (isNight) {
        // Moon – pale disc with a soft blue-white halo
        glColor3f(0.92f,0.92f,0.88f);
        glutSolidSphere(0.35f, 16, 16);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glColor4f(0.80f,0.85f,1.0f,0.10f);
        glutSolidSphere(0.75f, 16, 16);
        glDisable(GL_BLEND);
    } else {
        // Sun – warms toward orange near sunset (skyPhase ~1.0)
        float warm = clampF((skyPhase - 0.2f)/1.1f, 0.0f, 1.0f);
        glColor3f(1.0f, lerpF(0.95f,0.55f,warm), lerpF(0.80f,0.15f,warm));
        glutSolidSphere(0.45f, 16, 16);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glColor4f(1.0f,0.85f,0.5f,0.12f);
        glutSolidSphere(0.90f, 16, 16);
        glDisable(GL_BLEND);
    }

    glPopMatrix();
    glDepthMask(GL_TRUE);
    glEnable(GL_LIGHTING);
}

struct StarDesc { float az, el, twinkle; };
static const int NUM_STARS = 140;
static StarDesc stars[NUM_STARS];
static bool starsInitialised = false;

static void initStars()
{
    for (int i = 0; i < NUM_STARS; i++) {
        stars[i].az      = randF(0.0f, 2.0f*PI);
        stars[i].el       = randF(0.20f, 1.30f);   // upper hemisphere only
        stars[i].twinkle  = randF(0.0f, 6.28f);
    }
    starsInitialised = true;
}

void drawStars()
{
    if (!isNight) return;
    if (!starsInitialised) initStars();

    glDisable(GL_LIGHTING);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glPointSize(1.6f);

    float t = glutGet(GLUT_ELAPSED_TIME) * 0.002f;
    const float R = 16.0f;
    glBegin(GL_POINTS);
    for (int i = 0; i < NUM_STARS; i++) {
        float twinkleA = 0.35f + 0.55f * (0.5f + 0.5f*sinf(t*2.5f + stars[i].twinkle));
        glColor4f(1.0f,1.0f,1.0f,twinkleA);
        float x = R * cosf(stars[i].el) * cosf(stars[i].az);
        float y = 2.0f + R * sinf(stars[i].el);
        float z = R * cosf(stars[i].el) * sinf(stars[i].az) - 4.0f;
        glVertex3f(x,y,z);
    }
    glEnd();

    glPointSize(1.0f);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

// ═══════════════════════════════════════════════════════════
//  § 1E  CLOUD SHADOWS  (Quick win #8)
//  Cheap dark, soft-edged ellipses projected straight down onto
//  the ground at each cloud's (scrolled) X/Z position.
// ═══════════════════════════════════════════════════════════
void drawCloudShadows()
{
    if (isNight) return;   // not meaningfully visible after dark

    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    for (int i = 0; i < NUM_CLOUDS; i++) {
        const CloudDesc& c = CLOUDS[i];
        float cx = c.x + cloudOffset;
        if (cx >  6.0f) cx -= 12.0f;
        if (cx < -6.0f) cx += 12.0f;

        glColor4f(0.0f,0.0f,0.0f,0.09f);
        glPushMatrix();
            glTranslatef(cx, 0.006f, c.z);
            glScalef(c.rx*1.3f, 1.0f, c.rz*1.3f);
            glBegin(GL_TRIANGLE_FAN);
                glNormal3f(0,1,0);
                glVertex3f(0,0,0);
                for (int k=0;k<=16;k++) {
                    float a = 2.0f*PI*k/16.0f;
                    glVertex3f(cosf(a), 0, sinf(a));
                }
            glEnd();
        glPopMatrix();
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

// ═══════════════════════════════════════════════════════════
//  § 1F  WET ROAD OVERLAY  (Quick win #10 – puddle reflections)
//  A thin, low-alpha, additive-blended sheen over the road quads
//  while it's raining, so the asphalt reads as reflective/wet
//  instead of unchanged matte tarmac.
// ═══════════════════════════════════════════════════════════
void drawWetRoadOverlay()
{
    if (!isRaining) return;

    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);

    glColor4f(0.55f,0.62f,0.72f,0.10f);
    flatQuad(-4.0f,-1.0f,4.0f,1.0f,0.013f);
    flatQuad(0.8f,-4.0f,2.8f,4.0f,0.013f);

    // A brighter streak roughly under where lit street lamps / signals
    // would catch the wet surface - cheap but sells the "reflective" look.
    if (isNight) {
        glColor4f(0.9f,0.9f,0.7f,0.10f);
        flatQuad(-1.2f,-1.0f,1.2f,1.0f,0.014f);
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

// ═══════════════════════════════════════════════════════════
//  § 3  PARTICLE SYSTEM  –  Initialise & Helpers
// ═══════════════════════════════════════════════════════════
void initParticles()
{
    for (int i = 0; i < MAX_SMOKE;      i++) smokeParticles[i].active = false;
    for (int i = 0; i < MAX_SPLASH;     i++) splashParticles[i].active = false;
    for (int i = 0; i < MAX_BASE_SMOKE; i++) baseSmokeParticles[i].active = false;
}

// ── Emit one chimney smoke particle at chimney index ci ──────
static void emitSmokeParticle(int ci)
{
    // Find an inactive slot in the chimney's partition
    int base  = ci * SMOKE_PER_CHIMNEY;
    int limit = base + SMOKE_PER_CHIMNEY;
    for (int i = base; i < limit; i++) {
        Particle& p = smokeParticles[i];
        if (!p.active) {
            p.x     = CHIMNEY_X[ci] + randF(-0.04f, 0.04f);
            p.y     = CHIMNEY_Y_BASE;
            p.z = CHIMNEY_Z_COORD - 0.75f + randF(-0.04f, 0.04f);
            p.vx    = randF(-0.004f, 0.004f);  // jitter
            p.vy    = randF( 0.008f, 0.018f);  // rises
            p.vz    = randF(-0.004f, 0.004f);
            p.size  = randF(0.04f, 0.07f);
            p.alpha = randF(0.5f,  0.75f);
            p.life  = 1.0f;
            p.active= true;
            break;
        }
    }
}

// ── Emit base-smoke for destroyed buildings ──────────────────
static void emitBaseSmokeParticle(float bx, float bz)
{
    for (int i = 0; i < MAX_BASE_SMOKE; i++) {
        Particle& p = baseSmokeParticles[i];
        if (!p.active) {
            p.x     = bx + randF(-0.2f, 0.2f);
            p.y     = 0.05f;
            p.z     = bz + randF(-0.2f, 0.2f);
            p.vx    = randF(-0.006f, 0.006f);
            p.vy    = randF(0.010f, 0.025f);
            p.vz    = randF(-0.006f, 0.006f);
            p.size  = randF(0.06f, 0.12f);
            p.alpha = randF(0.60f, 0.90f);
            p.life  = 1.0f;
            p.active= true;
            break;
        }
    }
}

// ── Emit rain splash at random ground position ───────────────
static void emitSplashParticle()
{
    for (int i = 0; i < MAX_SPLASH; i++) {
        Particle& p = splashParticles[i];
        if (!p.active) {
            p.x     = randF(-3.8f, 3.8f);
            p.y     = 0.002f;
            p.z     = randF(-3.8f, 3.8f);
            p.vx    = randF(-0.006f, 0.006f);
            p.vy    = randF( 0.005f, 0.015f);
            p.vz    = randF(-0.006f, 0.006f);
            p.size  = randF(0.015f, 0.04f);
            p.alpha = 0.80f;
            p.life  = 1.0f;
            p.active= true;
            break;
        }
    }
}

// ── Update chimney smoke each timer tick ─────────────────────
void updateSmokeParticles()
{
    // Only emit if factory is alive
    if (bState[4].destroyProgress < 0.99f) {
        for (int ci = 0; ci < NUM_CHIMNEYS; ci++) {
            if (rand() % 4 == 0) emitSmokeParticle(ci);
        }
    }

    // Update base smoke for destroyed buildings
    for (int bi = 0; bi < NUM_BUILDINGS; bi++) {
        if (bState[bi].emittingBaseSmoke && rand() % 3 == 0)
            emitBaseSmokeParticle(BLDG_CX[bi], BLDG_CZ[bi]);
    }

    for (int i = 0; i < MAX_SMOKE; i++) {
        Particle& p = smokeParticles[i];
        if (!p.active) continue;
        p.x    += p.vx + randF(-0.002f, 0.002f);  // random jitter on X
        p.y    += p.vy;
        p.z    += p.vz + randF(-0.002f, 0.002f);  // random jitter on Z
        p.size += 0.003f;     // grows as it rises
        p.alpha-= 0.012f;     // fades out
        p.life -= 0.015f;
        if (p.life <= 0.0f || p.alpha <= 0.0f) p.active = false;
    }
    for (int i = 0; i < MAX_BASE_SMOKE; i++) {
        Particle& p = baseSmokeParticles[i];
        if (!p.active) continue;
        p.x    += p.vx + randF(-0.003f, 0.003f);
        p.y    += p.vy;
        p.z    += p.vz + randF(-0.003f, 0.003f);
        p.size += 0.005f;
        p.alpha-= 0.015f;
        p.life -= 0.018f;
        if (p.life <= 0.0f || p.alpha <= 0.0f) p.active = false;
    }
}

// ── Update splash particles when raining ─────────────────────
void updateSplashParticles()
{
    if (!isRaining) { for (int i=0;i<MAX_SPLASH;i++) splashParticles[i].active=false; return; }
    if (rand() % 3 == 0) emitSplashParticle();

    for (int i = 0; i < MAX_SPLASH; i++) {
        Particle& p = splashParticles[i];
        if (!p.active) continue;
        p.x    += p.vx;
        p.y    += p.vy;
        p.vy   -= 0.003f;    // gravity
        p.z    += p.vz;
        p.alpha-= 0.08f;
        p.life -= 0.12f;
        if (p.life <= 0.0f || p.y < 0.0f) p.active = false;
    }
}

// ── Draw chimney smoke ────────────────────────────────────────
void drawSmokeParticles()
{
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    // Chimney smoke – grey/white puffs
    for (int i = 0; i < MAX_SMOKE; i++) {
        const Particle& p = smokeParticles[i];
        if (!p.active) continue;
        float grey = 0.72f + p.life * 0.20f;  // whiter when fresh
        glColor4f(grey, grey, grey, p.alpha);
        glPushMatrix();
            glTranslatef(p.x, p.y, p.z);
            glutSolidSphere(p.size, 6, 4);
        glPopMatrix();
    }

    // Base smoke – darker (black smoke from destruction)
    for (int i = 0; i < MAX_BASE_SMOKE; i++) {
        const Particle& p = baseSmokeParticles[i];
        if (!p.active) continue;
        float dark = 0.15f + p.life * 0.15f;
        glColor4f(dark, dark, dark, p.alpha);
        glPushMatrix();
            glTranslatef(p.x, p.y, p.z);
            glutSolidSphere(p.size, 6, 4);
        glPopMatrix();
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

// ── Draw rain splashes at ground level ───────────────────────
// Quick win: ripples. Rendered as an expanding, fading ring (rather than a
// static flat quad) so each splash reads as an actual ripple spreading out
// on the wet road / river surface as its particle life counts down.
void drawSplashParticles()
{
    if (!isRaining) return;

    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glLineWidth(1.4f);

    const int RING_SEGS = 10;
    for (int i = 0; i < MAX_SPLASH; i++) {
        const Particle& p = splashParticles[i];
        if (!p.active) continue;
        float age    = 1.0f - p.life;                    // 0 (new) → 1 (old)
        float ringR  = p.size * (1.0f + age * 4.0f);      // ripple grows outward
        glColor4f(0.85f, 0.92f, 1.0f, p.alpha * (1.0f - age));
        glBegin(GL_LINE_LOOP);
        for (int k = 0; k < RING_SEGS; k++) {
            float a = 2.0f * PI * k / RING_SEGS;
            glVertex3f(p.x + ringR*cosf(a), p.y, p.z + ringR*sinf(a));
        }
        glEnd();
    }

    glLineWidth(1.0f);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

// ═══════════════════════════════════════════════════════════
//  § 4  GOD MODE  –  Building Interaction
// ═══════════════════════════════════════════════════════════

// Called when a building is left-clicked
void onBuildingClick(int bldgIdx)
{
    if (bldgIdx < 0 || bldgIdx >= NUM_BUILDINGS) return;

    BuildingState& bs = bState[bldgIdx];
    if (bs.isDestroyed) return;  // already gone, nothing to do

    if (meteorMode) {
        // ── METEOR MODE: trigger destruction ──────────────
        bs.isDestroyed     = true;
        bs.emittingBaseSmoke = true;
        // destroyProgress animated by update_destroy timer
    } else {
        // ── NORMAL MODE: cycle wall colour ────────────────
        bs.colorCycle = (bs.colorCycle + 1) % 5;
    }
}

// Update destroy animation (called every 30ms)
void updateDestroyAnimations()
{
    for (int i = 0; i < NUM_BUILDINGS; i++) {
        BuildingState& bs = bState[i];
        if (bs.isDestroyed && bs.destroyProgress < 1.0f) {
            bs.destroyProgress += 0.012f;    // scale collapses over ~1.4 seconds
            if (bs.destroyProgress >= 1.0f) {
                bs.destroyProgress = 1.0f;
                bs.heightScale     = 0.0f;
            } else {
                bs.heightScale = 1.0f - bs.destroyProgress;
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════
//  LIGHTING SETUP  –  modified for power outage + streetlights
// ═══════════════════════════════════════════════════════════
void setupLighting()
{
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    if (powerOutage) {
        // Minimal ambient only – effectively dark everywhere
        GLfloat darkAmb[4]  = {0.04f,0.04f,0.06f,1.0f};
        GLfloat darkDiff[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        GLfloat pos[4]      = {0.0f,10.0f,0.0f,1.0f};
        glLightfv(GL_LIGHT0,GL_POSITION,pos);
        glLightfv(GL_LIGHT0,GL_AMBIENT, darkAmb);
        glLightfv(GL_LIGHT0,GL_DIFFUSE, darkDiff);
        glDisable(GL_LIGHT1); glDisable(GL_LIGHT2);
        glDisable(GL_LIGHT3); glDisable(GL_LIGHT4);
        return;
    }

    // Blend day/night lighting based on skyPhase
    float nightBlend = clampF((skyPhase - 1.0f) / 1.0f, 0.0f, 1.0f); // 0=day, 1=night
    float dayBlend   = 1.0f - nightBlend;

    // Sun (day) or moon (night) – GL_LIGHT0
    GLfloat pos0[4] = { lerpF(5.0f,0.0f,nightBlend), lerpF(8.0f,10.0f,nightBlend), lerpF(3.0f,0.0f,nightBlend), 1.0f };
    GLfloat amb0[4] = { lerpF(0.30f,0.06f,nightBlend), lerpF(0.30f,0.06f,nightBlend), lerpF(0.28f,0.12f,nightBlend), 1.0f };
    GLfloat dif0[4] = { lerpF(1.0f,0.08f,nightBlend), lerpF(0.95f,0.08f,nightBlend), lerpF(0.85f,0.15f,nightBlend), 1.0f };
    GLfloat spc0[4] = { lerpF(0.6f,0.0f,nightBlend),  lerpF(0.6f,0.0f,nightBlend),  lerpF(0.5f,0.0f,nightBlend),  1.0f };
    glLightfv(GL_LIGHT0,GL_POSITION,pos0);
    glLightfv(GL_LIGHT0,GL_AMBIENT, amb0);
    glLightfv(GL_LIGHT0,GL_DIFFUSE, dif0);
    glLightfv(GL_LIGHT0,GL_SPECULAR,spc0);

    // Fill light (day only)
    if (dayBlend > 0.1f) {
        glEnable(GL_LIGHT1);
        GLfloat fp[4]={-4.0f,3.0f,-2.0f,1.0f};
        GLfloat fa[4]={0.10f,0.12f,0.15f,1.0f};
        GLfloat fd[4]={0.25f*dayBlend,0.25f*dayBlend,0.30f*dayBlend,1.0f};
        glLightfv(GL_LIGHT1,GL_POSITION,fp); glLightfv(GL_LIGHT1,GL_AMBIENT,fa); glLightfv(GL_LIGHT1,GL_DIFFUSE,fd);
    } else { glDisable(GL_LIGHT1); }

    // Street-lamp point light (night only) – GL_LIGHT2
    if (isNight) {
        glEnable(GL_LIGHT2);
        GLfloat lp[4]={-3.4f,0.85f,0.9f,1.0f};
        GLfloat ld[4]={0.80f,0.75f,0.40f,1.0f};
        GLfloat la[4]={0.05f,0.05f,0.02f,1.0f};
        glLightfv(GL_LIGHT2,GL_POSITION,lp); glLightfv(GL_LIGHT2,GL_DIFFUSE,ld); glLightfv(GL_LIGHT2,GL_AMBIENT,la);
        glLightf(GL_LIGHT2,GL_CONSTANT_ATTENUATION,0.5f);
        glLightf(GL_LIGHT2,GL_LINEAR_ATTENUATION,0.5f);
        glLightf(GL_LIGHT2,GL_QUADRATIC_ATTENUATION,0.1f);

        // GL_LIGHT3 – second street cluster (east side of road)
        glEnable(GL_LIGHT3);
        GLfloat lp3[4]={2.9f,0.85f,0.9f,1.0f};
        GLfloat ld3[4]={0.75f,0.70f,0.38f,1.0f};
        glLightfv(GL_LIGHT3,GL_POSITION,lp3); glLightfv(GL_LIGHT3,GL_DIFFUSE,ld3); glLightfv(GL_LIGHT3,GL_AMBIENT,la);
        glLightf(GL_LIGHT3,GL_CONSTANT_ATTENUATION,0.5f);
        glLightf(GL_LIGHT3,GL_LINEAR_ATTENUATION,0.4f);
        glLightf(GL_LIGHT3,GL_QUADRATIC_ATTENUATION,0.1f);
    } else {
        glDisable(GL_LIGHT2);
        glDisable(GL_LIGHT3);
    }
    glDisable(GL_LIGHT4);
}

// ═══════════════════════════════════════════════════════════
//  HOVER DETECTION  (unchanged)
// ═══════════════════════════════════════════════════════════
void updateHover(int mx, int my)
{
    GLdouble modelM[16], projM[16]; GLint viewport[4];
    glGetDoublev(GL_MODELVIEW_MATRIX,modelM);
    glGetDoublev(GL_PROJECTION_MATRIX,projM);
    glGetIntegerv(GL_VIEWPORT,viewport);
    int wy=viewport[3]-my;
    GLdouble nx,ny,nz,fx,fy,fz;
    if (!gluUnProject(mx,wy,0.0,modelM,projM,viewport,&nx,&ny,&nz)) { hoveredObj=-1; return; }
    if (!gluUnProject(mx,wy,1.0,modelM,projM,viewport,&fx,&fy,&fz)) { hoveredObj=-1; return; }
    double dy=fy-ny;
    if (fabs(dy)<1e-6) { hoveredObj=-1; return; }
    double t=(0.0-ny)/dy;
    if (t<0.0) { hoveredObj=-1; return; }
    float wx_=(float)(nx+t*(fx-nx));
    float wz_=(float)(nz+t*(fz-nz));
    hoveredObj=-1;
    for (int i=0;i<NUM_PICK;i++) {
        float ddx=wx_-PICK_TARGETS[i].cx, ddz=wz_-PICK_TARGETS[i].cz;
        if (sqrtf(ddx*ddx+ddz*ddz)<PICK_TARGETS[i].radius) { hoveredObj=i; break; }
    }
}

// ═══════════════════════════════════════════════════════════
//  HOVER HIGHLIGHT  (unchanged)
// ═══════════════════════════════════════════════════════════
void drawHoverHighlight(int idx)
{
    if (idx<0||idx>=NUM_PICK) return;
    if (bState[idx].destroyProgress >= 0.99f) return;  // don't highlight rubble

    const HighlightBox& hb=HIGHLIGHT_BOXES[idx];
    glDisable(GL_LIGHTING); glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    float pulse=0.55f+0.45f*sinf(glutGet(GLUT_ELAPSED_TIME)*0.004f);

    // Colour changes in Meteor Mode to warn of impending destruction
    float hr=1.0f, hg=0.92f, hb_=0.25f;
    if (meteorMode) { hr=1.0f; hg=0.20f; hb_=0.10f; }

    glColor4f(hr,hg,hb_,0.07f*pulse);
    glPushMatrix(); glTranslatef(hb.cx,hb.h*0.52f,hb.cz); glScalef(hb.w*1.14f,hb.h*1.14f,hb.d*1.14f); glutSolidCube(1.0f); glPopMatrix();
    glPolygonMode(GL_FRONT_AND_BACK,GL_LINE); glLineWidth(2.2f);
    glColor4f(hr,hg,hb_,pulse);
    glPushMatrix(); glTranslatef(hb.cx,hb.h*0.52f,hb.cz); glScalef(hb.w*1.14f,hb.h*1.14f,hb.d*1.14f); glutSolidCube(1.0f); glPopMatrix();
    glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    glPointSize(5.0f); glColor4f(1.0f,1.0f,0.4f,pulse);
    float hw=hb.w*0.57f, hh_=hb.h*1.14f, hd=hb.d*0.57f;
    glBegin(GL_POINTS);
        glVertex3f(hb.cx-hw,0.02f,hb.cz-hd); glVertex3f(hb.cx+hw,0.02f,hb.cz-hd);
        glVertex3f(hb.cx-hw,0.02f,hb.cz+hd); glVertex3f(hb.cx+hw,0.02f,hb.cz+hd);
        glVertex3f(hb.cx-hw,0.02f+hh_,hb.cz-hd); glVertex3f(hb.cx+hw,0.02f+hh_,hb.cz-hd);
        glVertex3f(hb.cx-hw,0.02f+hh_,hb.cz+hd); glVertex3f(hb.cx+hw,0.02f+hh_,hb.cz+hd);
    glEnd();
    glDisable(GL_BLEND); glLineWidth(1.0f); glPointSize(1.0f); glEnable(GL_LIGHTING);
}

// ═══════════════════════════════════════════════════════════
//  HUD  –  updated for new mode indicators
// ═══════════════════════════════════════════════════════════
static void hudString(float x, float y, const char* s)
{
    glRasterPos2f(x,y);
    for (;*s;++s) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12,*s);
}

void drawHUD()
{
    int w=glutGet(GLUT_WINDOW_WIDTH), h=glutGet(GLUT_WINDOW_HEIGHT);
    if (h==0) h=1;
    glDisable(GL_LIGHTING); glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); gluOrtho2D(0,w,0,h);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

    // Bottom bar
    glColor4f(0.0f,0.0f,0.0f,0.50f);
    glBegin(GL_QUADS); glVertex2f(0,0); glVertex2f(w,0); glVertex2f(w,22); glVertex2f(0,22); glEnd();
    glColor3f(0.75f,0.75f,0.75f);
    hudString(8,6,"RMB:Orbit | Scroll:Zoom | H:Reset | SPACE:Showcase"
                  " | LMB:Rain/Build | M:Meteor | P:Power | C:Sky | T:AutoSky | n/d | r/g/y:Signal | x:NoRain | ESC");

    // Showcase banner
    if (cam.autoRotate) {
        glColor4f(0,0,0,0.50f); glBegin(GL_QUADS); glVertex2f(w-230,h-30); glVertex2f(w,h-30); glVertex2f(w,h); glVertex2f(w-230,h); glEnd();
        glColor3f(0.30f,1.0f,0.55f); hudString(w-222,h-18,"SHOWCASE MODE  [SPACE to stop]");
    }

    // Hover tooltip
    if (hoveredObj>=0) {
        char buf[80];
        const char* mode = meteorMode ? " [CLICK=DESTROY]" : " [CLICK=COLOR]";
        sprintf(buf,"  %s%s  ", HOVER_NAMES[hoveredObj], mode);
        int len=(int)strlen(buf)*7;
        glColor4f(0,0,0,0.60f); glBegin(GL_QUADS);
            glVertex2f((float)(w/2-len/2-4),(float)(h-38)); glVertex2f((float)(w/2+len/2+4),(float)(h-38));
            glVertex2f((float)(w/2+len/2+4),(float)(h-18)); glVertex2f((float)(w/2-len/2-4),(float)(h-18));
        glEnd();
        float tc = meteorMode ? 0.0f : 0.95f;
        glColor3f(1.0f,tc,tc*0.30f);
        hudString((float)(w/2-len/2),(float)(h-32),buf);
    }

    // Mode indicators (top-left stack)
    float ty = (float)(h - 18);
    if (isNight) {
        glColor3f(0.55f,0.55f,1.0f); hudString(8,ty,"NIGHT"); ty-=18;
    }
    if (meteorMode) {
        glColor3f(1.0f,0.30f,0.10f); hudString(8,ty,"METEOR MODE [M]"); ty-=18;
    }
    if (powerOutage) {
        glColor3f(1.0f,0.80f,0.0f); hudString(8,ty,"POWER OUTAGE [P]"); ty-=18;
    }
    if (isRaining) {
        glColor3f(0.60f,0.80f,1.0f); hudString(8,ty,"RAIN + FOG"); ty-=18;
    }
    if (skyAutoAdvance) {
        glColor3f(0.80f,1.0f,0.60f); hudString(8,ty,"AUTO-SKY [T]"); ty-=18;
    }
    {
        bool hzR,hzY,hzG,vtR,vtY,vtG; getSignalStates(hzR,hzY,hzG,vtR,vtY,vtG);
        const char* sigTxt = signalAllRed ? "SIGNAL: ALL-RED (clearance)"
                           : hzG ? "SIGNAL: EW GREEN"
                           : hzY ? "SIGNAL: EW AMBER"
                           : vtG ? "SIGNAL: NS GREEN"
                           :       "SIGNAL: NS AMBER";
        if (hzG || vtG)      glColor3f(0.25f,1.0f,0.35f);
        else if (hzY || vtY) glColor3f(1.0f,0.80f,0.10f);
        else                 glColor3f(1.0f,0.30f,0.30f);
        hudString(8,ty,sigTxt); ty-=18;

        // Adaptive-signal debug readout: shows the same "sensor" data the
        // signal itself is deciding on, so the density-based logic is
        // visible/demonstrable rather than a black box.
        if (hzG || vtG) {
            bool hzNear, vtNear; computeRoadActivity(hzNear, vtNear);
            char adBuf[96];
            sprintf(adBuf,"  adaptive: %.1fs green (min %.0f / max %.0f) | EW:%s NS:%s",
                    signalClock, SIGNAL_MIN_GREEN, SIGNAL_MAX_GREEN,
                    hzNear?"car":"-", vtNear?"car":"-");
            glColor3f(0.65f,0.85f,0.65f);
            hudString(8,ty,adBuf); ty-=16;
        }
    }

    // Sky phase indicator (top-right)
    {
        char phaseBuf[32];
        const char* phaseName = skyPhase<0.7f?"DAY":(skyPhase<1.3f?"SUNSET":(skyPhase<2.3f?"NIGHT":"DAWN"));
        sprintf(phaseBuf,"Sky: %s  (C to cycle)", phaseName);
        glColor3f(0.85f,0.85f,0.70f);
        hudString((float)(w-220),(float)(h-45),phaseBuf);
    }

    glDisable(GL_BLEND);
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW);  glPopMatrix();
    glEnable(GL_DEPTH_TEST); glEnable(GL_LIGHTING);
}

// ═══════════════════════════════════════════════════════════
//  DISPLAY CALLBACK  –  full integration of all systems
// ═══════════════════════════════════════════════════════════
void display()
{
    // ① Advance sky phase + set glClearColor via lerp ──────
    smoothSkyTransition();

    // ② Apply traffic sensor braking ───────────────────────
    applyTrafficSensor();

    // ③ Clear screen ───────────────────────────────────────
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    int w=glutGet(GLUT_WINDOW_WIDTH), h=glutGet(GLUT_WINDOW_HEIGHT);
    if (h==0) h=1;

    // ④ Projection ─────────────────────────────────────────
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    gluPerspective(45.0,(double)w/h,0.1,200.0);

    // ⑤ Camera ─────────────────────────────────────────────
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    float cx,cy,cz; cam.getPosition(cx,cy,cz);
    gluLookAt(cx,cy,cz, 0.0,0.0,0.0, 0.0,1.0,0.0);

    updateHover(mouseX,mouseY);

    // ⑥ Lighting ───────────────────────────────────────────
    setupLighting();
    glEnable(GL_DEPTH_TEST);

    // ⑦ Skybox gradient – drawn before all 3D geometry ─────
    drawSkyboxGradient();
    drawSunMoon();
    drawStars();

    // ⑧ Volumetric fog ─────────────────────────────────────
    transitionFog();

    // ── Opaque scene geometry ──────────────────────────────
    drawGround();
    drawCloudShadows();
    drawRoads();
    drawWetRoadOverlay();
    drawRiver();
    drawBoats();
    drawHouses();
    drawShops();
    drawFactory(0.0f, 0.0f, -0.7f);
    drawWindmill(0.0f, 0.0f, 2.0f);
    drawTrees();
    drawFence();
    drawLamps();           // includes street light beams (blended)
    drawTrafficLights();
    drawCars();
    drawRain();

    // ── Transparent / particle systems ────────────────────
    drawSmokeParticles();
    drawSplashParticles();
    drawClouds();

    // ── Overlay effects ───────────────────────────────────
    if (hoveredObj >= 0) drawHoverHighlight(hoveredObj);

    // ── HUD (2D, no depth) ────────────────────────────────
    drawHUD();

    glutSwapBuffers();
}

// ═══════════════════════════════════════════════════════════
//  ANIMATION TIMERS  (original timers + new ones)
// ═══════════════════════════════════════════════════════════
// NOTE: cars only get a "free run, reset to cruise speed" reset while
// their road is FULLY green (not amber). applyTrafficSensor() -- called
// once per frame from display() -- owns all deceleration, so these
// timers just integrate position and let the sensor's speed value stick
// through the amber/red phases instead of stomping it back to full
// speed every 10ms.
// § Quick win: smooth acceleration. Instead of snapping straight back to
// cruise speed the instant the light turns green, ease towards it -
// speed += (target - speed) * rate - so pull-away looks like a real car
// accelerating rather than teleporting to full speed. Braking itself is
// already eased the same way inside applyTrafficSensor().
static const float CRUISE_SPEED = 0.01f;
static const float ACCEL_RATE   = 0.08f;   // how quickly speed eases toward cruise

void update_car1(int v){
    bool hzR,hzY,hzG,vtR,vtY,vtG; getSignalStates(hzR,hzY,hzG,vtR,vtY,vtG);
    if(hzG){ speed_c1 += (CRUISE_SPEED - speed_c1)*ACCEL_RATE; if(position_c1>2.7f) position_c1=-2.7f; }
    position_c1+=speed_c1;
    wheelSpin1 += (speed_c1*1.7f/WHEEL_RADIUS)*(180.0f/PI);
    if (wheelSpin1>36000.0f||wheelSpin1<-36000.0f) wheelSpin1=fmodf(wheelSpin1,360.0f);
    glutPostRedisplay(); glutTimerFunc(10,update_car1,0);
}
void update_car2(int v){
    bool hzR,hzY,hzG,vtR,vtY,vtG; getSignalStates(hzR,hzY,hzG,vtR,vtY,vtG);
    if(hzG){ speed_c2 += (CRUISE_SPEED - speed_c2)*ACCEL_RATE; if(position_c2<-2.7f) position_c2=2.7f; }
    position_c2-=speed_c2;
    wheelSpin2 -= (speed_c2*1.7f/WHEEL_RADIUS)*(180.0f/PI);
    if (wheelSpin2>36000.0f||wheelSpin2<-36000.0f) wheelSpin2=fmodf(wheelSpin2,360.0f);
    glutPostRedisplay(); glutTimerFunc(10,update_car2,0);
}
void update_car3(int v){
    bool hzR,hzY,hzG,vtR,vtY,vtG; getSignalStates(hzR,hzY,hzG,vtR,vtY,vtG);
    if(vtG){ speed_c3 += (CRUISE_SPEED - speed_c3)*ACCEL_RATE; if(position_c3>1.7f) position_c3=-1.7f; }
    position_c3+=speed_c3;
    wheelSpin3 += (speed_c3*1.7f/WHEEL_RADIUS)*(180.0f/PI);
    if (wheelSpin3>36000.0f||wheelSpin3<-36000.0f) wheelSpin3=fmodf(wheelSpin3,360.0f);
    glutPostRedisplay(); glutTimerFunc(10,update_car3,0);
}
void update_car4(int v){
    bool hzR,hzY,hzG,vtR,vtY,vtG; getSignalStates(hzR,hzY,hzG,vtR,vtY,vtG);
    if(vtG){ speed_c4 += (CRUISE_SPEED - speed_c4)*ACCEL_RATE; if(position_c4<-1.7f) position_c4=1.7f; }
    position_c4-=speed_c4;
    wheelSpin4 -= (speed_c4*1.7f/WHEEL_RADIUS)*(180.0f/PI);
    if (wheelSpin4>36000.0f||wheelSpin4<-36000.0f) wheelSpin4=fmodf(wheelSpin4,360.0f);
    glutPostRedisplay(); glutTimerFunc(10,update_car4,0);
}
void update_smoke(int v){
    if(position_s>2.1f) position_s=1.6f;
    position_s+=speed_s;
    glutPostRedisplay(); glutTimerFunc(100,update_smoke,0);
}
void update_river(int v){
    if(position_r<-0.5f) position_r=0.0f;
    position_r-=speed_r;
    riverWaveOffset += 0.08f;
    if (riverWaveOffset > 1000.0f) riverWaveOffset -= 1000.0f;
    glutPostRedisplay(); glutTimerFunc(100,update_river,0);
}
void update_boat1(int v){
    if(position_b1<-2.0f) position_b1=0.0f;
    position_b1-=speed_b1;
    glutPostRedisplay(); glutTimerFunc(100,update_boat1,0);
}
void update_boat2(int v){
    if(position_b2>0.0f) position_b2=-2.0f;
    position_b2+=speed_b2;
    glutPostRedisplay(); glutTimerFunc(100,update_boat2,0);
}
void update_rain(int v){
    if(position_rain<-0.1f) position_rain=0.3f;
    position_rain-=speed_rain;
    if(position_rain2>2.0f) position_rain2=1.8f;
    position_rain2+=speed_rain2;
    glutPostRedisplay(); glutTimerFunc(100,update_rain,0);
}
void update_windmill(int v){
    frameNumber++;
    glutPostRedisplay(); glutTimerFunc(30,update_windmill,0);
}
void update_camera(int v){
    cam.tick();
    glutTimerFunc(16,update_camera,0);
}
// Drives the green→amber→all-red→flip cycle. Fires every 100ms, so each
// tick represents 0.1 real seconds of signal-timer progress.
void update_signal(int v){
    advanceSignal(0.1f);
    glutPostRedisplay();
    glutTimerFunc(100,update_signal,0);
}

// ── NEW: Particle system update (~30fps) ─────────────────────
void update_particles(int v)
{
    updateSmokeParticles();
    updateSplashParticles();
    // Advance cloud scroll
    cloudOffset += 0.0012f;
    if (cloudOffset > 6.0f) cloudOffset -= 12.0f;
    glutPostRedisplay();
    glutTimerFunc(33, update_particles, 0);
}

// ── NEW: Building destroy animation (~30fps) ─────────────────
void update_destroy(int v)
{
    updateDestroyAnimations();
    glutPostRedisplay();
    glutTimerFunc(30, update_destroy, 0);
}

// ═══════════════════════════════════════════════════════════
//  INPUT CALLBACKS
// ═══════════════════════════════════════════════════════════
void button(unsigned char key, int mx, int my)
{
    switch (key) {
        case 'f': glutPostRedisplay(); break;
        // Manual "advance phase now" – skips straight to the amber
        // caution phase (or the all-red gap if already amber) instead
        // of snapping the light directly from green to red.
        case 'r':
            if (!signalYellow && !signalAllRed) { signalYellow = true; signalClock = 0.0f; }
            else if (signalYellow) { signalYellow = false; signalAllRed = true; signalClock = 0.0f; }
            glutPostRedisplay(); break;
        case 'g':
            cnt=0; signalYellow=false; signalAllRed=false; signalClock=0.0f;
            speed_c1=0.01f; speed_c2=0.01f; glutPostRedisplay(); break;
        case 'y': case 'Y':
            signalAuto = !signalAuto; glutPostRedisplay(); break;
        case 'n': flag++; isNight=true; skyPhase=2.0f; skyPhaseTarget=2.0f; glutPostRedisplay(); break;
        case 'd': flag=0; isNight=false; skyPhase=0.0f; skyPhaseTarget=0.0f;
                  position_s=1.6f; speed_s=0.01f; glutPostRedisplay(); break;
        case 'x': r=0; PlaySoundA(NULL,NULL,SND_ASYNC|SND_FILENAME); glutPostRedisplay(); break;

        // Camera
        case 'w': cam.elevation+=3.0f; cam.clamp(); glutPostRedisplay(); break;
        case 's': cam.elevation-=3.0f; cam.clamp(); glutPostRedisplay(); break;
        case 'a': cam.azimuth  -=3.0f;              glutPostRedisplay(); break;
        case 'q': cam.radius   -=0.4f; cam.clamp(); glutPostRedisplay(); break;
        case 'e': cam.radius   +=0.4f; cam.clamp(); glutPostRedisplay(); break;
        case 'h': case 'H': cam.lerpHome=true; cam.autoRotate=false; break;
        case ' ': cam.autoRotate=!cam.autoRotate; cam.lerpHome=false; glutPostRedisplay(); break;

        // § 2 – Power Outage toggle
        case 'p': case 'P':
            powerOutage = !powerOutage;
            glutPostRedisplay(); break;

        // § 4 – Meteor Mode toggle
        case 'm': case 'M':
            meteorMode = !meteorMode;
            glutPostRedisplay(); break;

        // § 1A – Force advance sky phase (Day→Sunset→Night→Day)
        case 'c': case 'C':
            // Snap to next phase in steps of 1.0
            skyPhaseTarget = fmodf(floorf(skyPhase) + 1.0f, 3.0f);
            glutPostRedisplay(); break;

        // § 1A – Toggle auto sky cycle
        case 't': case 'T':
            skyAutoAdvance = !skyAutoAdvance;
            glutPostRedisplay(); break;

        case 27: exit(0);
    }
}

void mouseButton(int btn, int state, int mx, int my)
{
    mouseX=mx; mouseY=my;

    if (btn == GLUT_LEFT_BUTTON && state == GLUT_DOWN) {
        // If hovering a building → interact; otherwise → toggle rain
        if (hoveredObj >= 0) {
            onBuildingClick(hoveredObj);
        } else {
            r++;
            PlaySoundA("rain.wav",NULL,SND_ASYNC|SND_FILENAME|SND_LOOP);
        }
        glutPostRedisplay();
        return;
    }
    if (btn == GLUT_RIGHT_BUTTON) {
        if (state==GLUT_DOWN) { cam.dragging=true; cam.lastMX=mx; cam.lastMY=my; cam.velAz=0; cam.velEl=0; cam.lerpHome=false; }
        else                    cam.dragging=false;
        return;
    }
    if (btn==3&&state==GLUT_DOWN) { cam.radius-=0.55f; cam.clamp(); glutPostRedisplay(); }
    if (btn==4&&state==GLUT_DOWN) { cam.radius+=0.55f; cam.clamp(); glutPostRedisplay(); }
}

void mouseMotion(int mx, int my)
{
    mouseX=mx; mouseY=my;
    if (cam.dragging) {
        int dx=mx-cam.lastMX, dy=my-cam.lastMY;
        float dAz=-(float)dx*cam.dragSens, dEl=(float)dy*cam.dragSens;
        cam.azimuth+=dAz; cam.elevation+=dEl; cam.clamp();
        cam.velAz=dAz; cam.velEl=dEl;
        cam.lastMX=mx; cam.lastMY=my;
        glutPostRedisplay();
    }
}

void mousePassive(int mx, int my)
{
    mouseX=mx; mouseY=my; glutPostRedisplay();
}

void reshape(int w, int h)
{
    if (h==0) h=1; glViewport(0,0,w,h);
}

// ═══════════════════════════════════════════════════════════
//  MAIN
// ═══════════════════════════════════════════════════════════
int main(int argc, char **argv)
{
    srand((unsigned int)time(NULL));
    initParticles();

    glutInit(&argc,argv);
    // GLUT_MULTISAMPLE requests a multisampled (anti-aliased) framebuffer
    // from the driver so every polygon edge in the scene isn't jagged -
    // this is the single highest-value "make it look realistic" change
    // for an immediate-mode OpenGL scene like this one.
    glutInitDisplayMode(GLUT_DOUBLE|GLUT_RGB|GLUT_DEPTH|GLUT_MULTISAMPLE);
    glutInitWindowSize(1430,800);
    glutInitWindowPosition(0,0);
    glutCreateWindow("Model City");

    cout << "===========================================\n";
    cout << "  Model City\n";
    cout << "===========================================\n\n";
    cout << "  [NEW CONTROLS]\n";
    cout << "  LMB on building : cycle colour / destroy (Meteor)\n";
    cout << "  M               : toggle Meteor Mode\n";
    cout << "  P               : toggle Power Outage\n";
    cout << "  C               : advance sky phase\n";
    cout << "  T               : auto-cycle sky\n\n";
    cout << "  [ORIGINAL CONTROLS]\n";
    cout << "  RMB drag         : orbit\n";
    cout << "  Scroll           : zoom\n";
    cout << "  H                : home\n";
    cout << "  SPACE            : showcase spin\n";
    cout << "  LMB (sky/road)   : toggle rain\n";
    cout << "  X                : stop rain\n";
    cout << "  n/d              : night/day\n";
    cout << "  r                : advance signal (green->amber->all-red->flip)\n";
    cout << "  g                : reset signal to horizontal green\n";
    cout << "  y                : toggle automatic ADAPTIVE signal cycling\n";
    cout << "  ESC              : quit\n\n";

    glEnable(GL_DEPTH_TEST);
    glShadeModel(GL_SMOOTH);
    glEnable(GL_NORMALIZE);
    glEnable(GL_MULTISAMPLE);   // smooths every edge in the scene (needs GLUT_MULTISAMPLE above)
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(button);
    glutMouseFunc(mouseButton);
    glutMotionFunc(mouseMotion);
    glutPassiveMotionFunc(mousePassive);

    // Original timers
    glutTimerFunc( 30, update_windmill, 0);
    glutTimerFunc(100, update_boat1,    0);
    glutTimerFunc(100, update_boat2,    0);
    glutTimerFunc(100, update_river,    0);
    glutTimerFunc(100, update_smoke,    0);
    glutTimerFunc( 10, update_car1,     0);
    glutTimerFunc( 10, update_car2,     0);
    glutTimerFunc( 10, update_car3,     0);
    glutTimerFunc( 10, update_car4,     0);
    glutTimerFunc(100, update_rain,     0);
    glutTimerFunc( 16, update_camera,   0);
    glutTimerFunc(100, update_signal,   0);   // auto green→amber→all-red cycle

    // New v3 timers
    glutTimerFunc( 33, update_particles, 0);   // particle simulation
    glutTimerFunc( 30, update_destroy,   0);   // meteor collapse animation

    glutMainLoop();
    return 0;
}