#include <jni.h>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <fstream>
#include <math.h>
#include <android/log.h>

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "FF_CHEAT", __VA_ARGS__)

// ==========================================
// 1. ESTRUTURAS UNITY
// ==========================================
struct Vector3 { float x, y, z; };
struct Matrix4x4 { float m[16]; };
struct Quaternion { float x, y, z, w; };

// ==========================================
// 2. OFFSETS (ARM64)
// ==========================================
constexpr uintptr_t OFF_GET_MAIN_CAMERA   = 0xA0CE614;
constexpr uintptr_t OFF_GET_VIEW_MATRIX   = 0xA0CDC80;
constexpr uintptr_t OFF_GET_PLAYER_LIST   = 0x72F0780;
constexpr uintptr_t OFF_GET_TRANSFORM     = 0xA116240;
constexpr uintptr_t OFF_GET_POSITION      = 0xA124738;
constexpr uintptr_t OFF_GET_CUR_HP        = 0x7083044;
constexpr uintptr_t OFF_GET_IS_DEAD       = 0x724B47C;
constexpr uintptr_t OFF_GET_ROTATION      = 0xA124928;
constexpr uintptr_t OFF_SET_ROTATION      = 0xA1249B0;
constexpr uintptr_t OFF_GET_HEAD_BONE     = 0x701F37C;
constexpr uintptr_t OFF_LOOK_ROTATION     = 0xA10C2A0;

// ==========================================
// 3. TIPOS (PONTEIROS DE FUNÇÃO)
// ==========================================
typedef void* (*GetMainCamera_t)();
typedef void* (*GetViewMatrix_t)(void*);
typedef void* (*GetPlayerList_t)();
typedef void* (*GetTransform_t)(void*);
typedef Vector3 (*GetPosition_t)(void*);
typedef int (*GetCurHP_t)(void*);
typedef bool (*GetIsDead_t)(void*);
typedef void* (*GetHeadBone_t)(void*);
typedef Quaternion (*LookRotation_t)(Vector3);
typedef void (*SetRotation_t)(void*, Quaternion);
typedef Quaternion (*GetRotation_t)(void*);

// ==========================================
// 4. MATEMÁTICA E UTILITÁRIOS
// ==========================================
uintptr_t get_module_base(const char* module_name) {
    FILE *fp = fopen("/proc/self/maps", "rt");
    if (fp == NULL) return 0;
    char line[512];
    uintptr_t addr = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, module_name)) { sscanf(line, "%lx", &addr); break; }
    }
    fclose(fp);
    return addr;
}

float GetDistance(Vector3 a, Vector3 b) {
    return sqrt(pow(b.x - a.x, 2) + pow(b.y - a.y, 2) + pow(b.z - a.z, 2));
}

bool WorldToScreen(Vector3 pos, Vector3& screen, Matrix4x4 matrix, float width, float height) {
    float w = matrix.m[3] * pos.x + matrix.m[7] * pos.y + matrix.m[11] * pos.z + matrix.m[15];
    if (w < 0.01f) return false;
    float x = matrix.m[0] * pos.x + matrix.m[4] * pos.y + matrix.m[8] * pos.z + matrix.m[12];
    float y = matrix.m[1] * pos.x + matrix.m[5] * pos.y + matrix.m[9] * pos.z + matrix.m[13];
    screen.x = (width / 2.0f) * (1.0f + x / w);
    screen.y = (height / 2.0f) * (1.0f - y / w);
    return true;
}

Quaternion Slerp(Quaternion q1, Quaternion q2, float t) {
    float dot = q1.x * q2.x + q1.y * q2.y + q1.z * q2.z + q1.w * q2.w;
    if (dot < 0.0f) { q2.x = -q2.x; q2.y = -q2.y; q2.z = -q2.z; q2.w = -q2.w; dot = -dot; }
    if (dot > 0.9995f) {
        Quaternion result;
        result.x = q1.x + t * (q2.x - q1.x); result.y = q1.y + t * (q2.y - q1.y);
        result.z = q1.z + t * (q2.z - q1.z); result.w = q1.w + t * (q2.w - q1.w);
        return result;
    }
    float theta_0 = acosf(dot); float theta = theta_0 * t;
    float sin_theta = sinf(theta); float sin_theta_0 = sinf(theta_0);
    float s0 = cosf(theta) - dot * sin_theta / sin_theta_0;
    float s1 = sin_theta / sin_theta_0;
    Quaternion result;
    result.x = (q1.x * s0) + (q2.x * s1); result.y = (q1.y * s0) + (q2.y * s1);
    result.z = (q1.z * s0) + (q2.z * s1); result.w = (q1.w * s0) + (q2.w * s1);
    return result;
}

// ==========================================
// 5. LÓGICA DO ESP
// ==========================================
void DrawESP(uintptr_t base, float width, float height) {
    if (!base) return;
    std::ifstream file("/data/local/tmp/ff_esp_box");
    if (!file.is_open()) return;
    std::string val; file >> val; file.close();
    if (val != "1") return;

    GetMainCamera_t GetMainCamera = (GetMainCamera_t)(base + OFF_GET_MAIN_CAMERA);
    GetViewMatrix_t GetViewMatrix = (GetViewMatrix_t)(base + OFF_GET_VIEW_MATRIX);
    GetPlayerList_t GetPlayerList = (GetPlayerList_t)(base + OFF_GET_PLAYER_LIST);
    GetTransform_t GetTransform   = (GetTransform_t)(base + OFF_GET_TRANSFORM);
    GetPosition_t GetPosition     = (GetPosition_t)(base + OFF_GET_POSITION);
    GetCurHP_t GetCurHP           = (GetCurHP_t)(base + OFF_GET_CUR_HP);
    GetIsDead_t GetIsDead         = (GetIsDead_t)(base + OFF_GET_IS_DEAD);

    void* camera = GetMainCamera();
    if (!camera) return;
    Matrix4x4 viewMatrix;
    void* matrixPtr = GetViewMatrix(camera);
    if (!matrixPtr) return;
    memcpy(&viewMatrix, matrixPtr, sizeof(Matrix4x4));

    void* playerList = GetPlayerList();
    if (!playerList) return;
    void* itemsArray = *(void**)((uintptr_t)playerList + 0x10);
    int size = *(int*)((uintptr_t)playerList + 0x18);
    if (!itemsArray || size <= 0 || size > 100) return;

    for (int i = 0; i < size; i++) {
        void* player = *(void**)((uintptr_t)itemsArray + 0x20 + (i * 8));
        if (!player || GetIsDead(player) || GetCurHP(player) <= 0) continue;
        void* transform = GetTransform(player);
        if (!transform) continue;
        
        Vector3 worldPos = GetPosition(transform);
        Vector3 screenPos;
        
        if (WorldToScreen(worldPos, screenPos, viewMatrix, width, height)) {
            // AQUI ENTRA O IMGUI
            LOGD("Player ESP: X %f Y %f", screenPos.x, screenPos.y);
        }
    }
}

// ==========================================
// 6. LÓGICA DO AIMBOT
// ==========================================
void RunAimbot(uintptr_t base, void* localPlayer) {
    if (!localPlayer) return;
    std::ifstream file("/data/local/tmp/ff_aim");
    if (!file.is_open()) return;
    std::string val; file >> val; file.close();
    if (val != "1") return;

    GetPlayerList_t GetPlayerList = (GetPlayerList_t)(base + OFF_GET_PLAYER_LIST);
    GetHeadBone_t GetHeadBone     = (GetHeadBone_t)(base + OFF_GET_HEAD_BONE);
    GetTransform_t GetTransform   = (GetTransform_t)(base + OFF_GET_TRANSFORM);
    GetPosition_t GetPosition     = (GetPosition_t)(base + OFF_GET_POSITION);
    LookRotation_t LookRotation   = (LookRotation_t)(base + OFF_LOOK_ROTATION);
    GetIsDead_t GetIsDead         = (GetIsDead_t)(base + OFF_GET_IS_DEAD);
    SetRotation_t SetRotation     = (SetRotation_t)(base + OFF_SET_ROTATION);
    GetRotation_t GetRotation     = (GetRotation_t)(base + OFF_GET_ROTATION);

    void* myTransform = GetTransform(localPlayer);
    Vector3 myPos = GetPosition(myTransform);

    void* playerList = GetPlayerList();
    if (!playerList) return;
    void* itemsArray = *(void**)((uintptr_t)playerList + 0x10);
    int size = *(int*)((uintptr_t)playerList + 0x18);
    if (!itemsArray || size <= 0 || size > 100) return;

    void* bestTarget = nullptr;
    float closestDist = 999999.0f;

    for (int i = 0; i < size; i++) {
        void* player = *(void**)((uintptr_t)itemsArray + 0x20 + (i * 8));
        if (!player || player == localPlayer || GetIsDead(player)) continue;

        void* enemyHeadTF = GetHeadBone(player);
        if (!enemyHeadTF) continue;
        
        Vector3 enemyHeadPos = GetPosition(enemyHeadTF);
        float dist = GetDistance(myPos, enemyHeadPos);

        if (dist < closestDist && dist < 150.0f) { 
            closestDist = dist;
            bestTarget = enemyHeadTF;
        }
    }

    if (bestTarget) {
        Vector3 targetPos = GetPosition(bestTarget);
        Vector3 direction = { targetPos.x - myPos.x, targetPos.y - myPos.y, targetPos.z - myPos.z };

        Quaternion aimRotation = LookRotation(direction);
        Quaternion currentRotation = GetRotation(myTransform);

        float smoothFactor = 0.15f; 
        Quaternion smoothRotation = Slerp(currentRotation, aimRotation, smoothFactor);

        SetRotation(myTransform, smoothRotation);
    }
}

// ==========================================
// 7. INJETOR (PTRACE + MEMFD)
// ==========================================
int get_pid(const char* process_name) {
    DIR* dir = opendir("/proc"); if (!dir) return -1;
    struct dirent* entry; int pid = -1;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            int current_pid = atoi(entry->d_name);
            if (current_pid > 0) {
                char cmdline_path[64];
                snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%d/cmdline", current_pid);
                FILE* f = fopen(cmdline_path, "r");
                if (f) {
                    char cmdline[256];
                    if (fgets(cmdline, sizeof(cmdline), f) && strstr(cmdline, process_name)) {
                        pid = current_pid; fclose(f); break;
                    }
                    fclose(f);
                }
            }
        }
    }
    closedir(dir); return pid;
}

extern "C" JNIEXPORT void JNICALL
Java_com_allhack_cheats_NativeBridge_injectCheat(JNIEnv* env, jclass clazz, jstring processName, jstring soPath) {
    const char* process = env->GetStringUTFChars(processName, 0);
    const char* so_path = env->GetStringUTFChars(soPath, 0);
    int pid = get_pid(process);
    if (pid == -1) { LOGD("Jogo nao encontrado."); return; }

    int so_fd = open(so_path, O_RDONLY);
    if (so_fd < 0) return;
    struct stat st; fstat(so_fd, &st);
    void* so_data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, so_fd, 0);
    if (so_data == MAP_FAILED) { close(so_fd); return; }

    int mem_fd = syscall(__NR_memfd_create, "jit-cache", MFD_CLOEXEC);
    if (mem_fd < 0) { munmap(so_data, st.st_size); close(so_fd); return; }
    write(mem_fd, so_data, st.st_size);
    munmap(so_data, st.st_size); close(so_fd);

    char mem_path[64];
    snprintf(mem_path, sizeof(mem_path), "/proc/self/fd/%d", mem_fd);
    LOGD("Injetando memfd: %s", mem_path);
    
    env->ReleaseStringUTFChars(processName, process);
    env->ReleaseStringUTFChars(soPath, so_path);
}

// ==========================================
// 8. JNI PRINCIPAL (LOOP)
// ==========================================
extern "C" JNIEXPORT void JNICALL
Java_com_allhack_cheats_NativeBridge_updateESP(JNIEnv* env, jclass clazz, jint width, jint height) {
    uintptr_t base = get_module_base("libil2cpp.so");
    if (base) {
        typedef void* (*GetLocalPlayer_t)();
        GetLocalPlayer_t GetLocalPlayer = (GetLocalPlayer_t)(base + 0x75F0EE4);
        void* localPlayer = GetLocalPlayer();

        if (localPlayer) {
            DrawESP(base, (float)width, (float)height);
            RunAimbot(base, localPlayer);
        }
    }
}
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE    := ffhook
LOCAL_SRC_FILES := ffhook.cpp
LOCAL_LDLIBS    := -llog -ldl -landroid
include $(BUILD_SHARED_LIBRARY)
APP_ABI := arm64-v8a
APP_PLATFORM := android-21
APP_STL := c++_shared
