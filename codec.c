#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <windows.h>

int   filedecode(char *s);
char* fileencode(char *path);
char* fileencode2(char *path, char *data, int size);

#ifdef DEBUG
    #define puts(x) printf("(ln: %3d) %s\n", __LINE__, (x))
    #define log_num(x) printf("(ln: %3d) %s=%g\n", __LINE__, #x, (float)(x))
    #define log_str(x) printf("(ln: %3d) %s=\"%s\"\n", __LINE__, #x, (x))
#else
    #define puts(x)
    #define log_num(x)
    #define log_str(x)
#endif

#define RECV_FOLDER "recv\\"
static char b64map[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+_";

/* file encoder functions */

static char* b64encode(char *data, int len)
{
    if (!data) return 0;

    int len2 = (len + 2) * 4 / 3 - 1;
    char *ret = calloc(1, len2);

    unsigned char c, k;
    for (int i = 0, j = -1; i < len; i++) {
        c = data[i];
        k = i % 3;
        if (k == 0) {
            ret[++j] = c >> 2;
            ret[++j] = c << 4;
        } else if (k == 1) {
            ret[j]  |= c >> 4;
            ret[++j] = c << 2;
        } else if (k == 2) {
            ret[j]  |= c >> 6;
            ret[++j] = c;
        }
    }

    for (int j = 0; j < len2 - 1; j++)
        ret[j] = b64map[ret[j] & 63];

    return ret;
}

static char* b64read(char *path)
{
    if (!path) return 0;

    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;

    fseek(fp, 0, SEEK_END);
    int len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *data = calloc(1, len);
    fread(data, len, 1, fp);
    char* ret = b64encode(data, len);
    free(data);

    fclose(fp);
    return ret;
}

static char* basename(char *path)
{
    if (!path) return 0;

    char *file = strrchr(path, '\\');
    file = file ? file + 1 : path;
    for (int i = 0; file[i]; i++)
        if (file[i] == ',')
            file[i] = '-';
    return file;
}

char* fileencode(char *path)
{
    if (!path) return 0;

    char *data = b64read(path);
    if (!data) return 0;

    char *name = basename(path);
    char *ret = calloc(1, strlen(name) + strlen(data) + 2);
    strcat(ret, name);
    strcat(ret, ":");
    strcat(ret, data);

    free(data);
    return ret;
}

char* fileencode2(char *path, char *data, int size)
{
    if (!(path && data)) return 0;

    char *b64data = b64encode(data, size);
    if (!b64data) return 0;

    char *name = basename(path);
    char *ret = calloc(1, strlen(name) + strlen(b64data) + 2);
    strcat(ret, name);
    strcat(ret, ":");
    strcat(ret, b64data);

    free(b64data);
    return ret;
}

/* file decoder functions */

static char* b64join(char *s)
{
    if (!s) return 0;

    int ret;
    int len = strlen(s) + 1;

    // wash chars
    puts("wash chars");
    char *s1 = calloc(1, len);
    for (int i = 0, j = 0; s[i]; i++)
        if (!strchr(" \t\r\n", s[i]))
            s1[j++] = s[i];
    int len1 = strlen(s1) + 1;
    log_str(s1);
    log_num(len1);

    // get header
    puts("get header");
    int cnt2, sum2;
    ret = sscanf(s, "%d/%d:", &cnt2, &sum2);
    if (!(ret == 2 && 0 < cnt2 && cnt2 <= sum2)) {
        free(s1);
        return 0;
    }
    log_num(cnt2);
    log_num(sum2);

    // split segments
    puts("split segments");
    char **ss = calloc(sum2, sizeof(void*));
    char *s3 = calloc(1, len);
    int cnt3, sum3, len3 = 0;
    for (int i = 0; i < len1 - 1; i = i + len3) {
        ret = sscanf(s1 + i, "%d/%d:%[^,],%n", &cnt3, &sum3, s3, &len3);
        if (!(ret == 3 && len3 > 0 && sum2 == sum3 && 0 < cnt3 && cnt3 <= sum3)) {
            free(s1);
            free(s3);
            for (int z = 0; z < sum2; z++)
                free(ss[z]);
            free(ss);
            return 0;
        }
        ss[cnt3 - 1] = calloc(1, strlen(s1) + 1);
        strcpy(ss[cnt3 - 1], s3);
    }
    free(s1);
    free(s3);

    // join string
    puts("join string");
    char *s4 = calloc(1, len);
    char *s4p = s4;
    for (int i = 0; i < sum2; i++) {
        if (!ss[i]) {
            free(s4);
            return 0;
        }
        for (int j = 0; ss[i][j]; j++)
            *s4p++ = ss[i][j];
    }
    log_str(s4);

    // clear and return
    puts("clear and return");
    for (int z = 0; z < sum2; z++)
        free(ss[z]);
    free(ss);
    return s4;
}

static char* b64decode(char *s)
{
    if (!s) return 0;

    char *ret = calloc(1, strlen(s) * 3 / 4 + 1);
    char c, k;
    char *b64chr;

    for (int i = 0, j = -1; s[i]; i++) {
        b64chr = strchr(b64map, s[i]);
        if (!b64chr) {
            free(ret);
            return 0;
        }

        c = b64chr - b64map;
        k = i % 4;
        if (k == 0) {
            ret[++j] = c << 2;
        } else if (k == 1) {
            ret[j]  |= c >> 4;
            ret[++j] = c << 4;
        } else if (k == 2) {
            ret[j]  |= c >> 2;
            ret[++j] = c << 6;
        } else if (k == 3) {
            ret[j]  |= c;
        }
    }
    return ret;
}

static char* split(char *data, char sp)
{
    if (!data) return 0;

    char *ret = strchr(data, sp);
    if (ret)
        *ret++ = 0;
    return ret;
}

static void redir()
{
    char path[MAX_PATH];
    GetModuleFileNameA(0, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) {
        slash[1] = 0;
        chdir(path);
        log_str(path);
    }
}

static char* newname(char *file)
{
    if (!file) return 0;

    char *file2 = calloc(1, strlen(file) + 16);
    strcat(file2, RECV_FOLDER);
    strcat(file2, file);
    log_str(file2);

    char *ext = strrchr(file, '.');
    ext = ext ? ext : file + strlen(file);

    FILE *fp;
    for (int i = 2; fp = fopen(file2, "rb"); i++) {
        fclose(fp);
        sprintf(file2 + (ext - file) + strlen(RECV_FOLDER), "-%d%s", i, ext);
    }
    return file2;
}

static int b64write(char *file, char *b64_data)
{
    if (!file || !b64_data) return 0;

    int len = strlen(b64_data) * 3 / 4;
    char *data = b64decode(b64_data);
    if (!data) {
        return 0;
    }

    CreateDirectoryA(RECV_FOLDER, 0);
    FILE *fp = fopen(file, "wb");
    if (!fp) {
        free(data);
        return 0;
    }
    fwrite(data, len, 1, fp);
    fclose(fp);
    free(data);
    return 1; // success
}

static void show(char *path)
{
    if (!path) return;

    char args[strlen(path) + 32];
    sprintf(args, "/select,\"%s\"", path);
    ShellExecuteA(0, "open", "explorer", args, 0, SW_SHOWNORMAL);
}

int filedecode(char *s)
{
    if (!s) return 0;

    redir();
    char *head = b64join(s);
    char *data = split(head, ':');
    char *file2 = newname(head);
    int ret = b64write(file2, data);
    if (ret) show(file2);

    free(head);
    free(file2);
    return ret;
}

/* unit test */

#ifdef DEBUG

char* fileencode(char *p);
int   filedecode(char *s);

static void test()
{
#define TEST_ENCODE 1
#define TEST_DECODE 1

#if TEST_ENCODE
    puts("-----------");
    puts("TEST_ENCODE");

    char *s1_origin = "HelloWorld!"; // 11 chars
    char *s1_encode = b64encode(s1_origin, strlen(s1_origin));
    log_str(s1_origin);
    log_str(s1_encode);

    char *p1 = "HelloÊÀ½ç!.txt";
    FILE *fp = fopen(p1, "wb");
    fwrite(s1_origin, 1, strlen(s1_origin), fp);
    fclose(fp);
    puts(fileencode(p1));
#endif

#if TEST_DECODE
    puts("-----------");
    puts("TEST_DECODE");

    char *s2_origin = "SGVsbG9Xb3JsZCE";
    char *s2_decode = b64decode(s2_origin);
    log_str(s2_origin);
    log_str(s2_decode);

    filedecode("1/2:test.txt:SGVsbG, 2/2:9Xb3JsZCE,");
#endif

#if TEST_ENCODE && TEST_DECODE
    puts("-----------");
    puts("TEST_ENCODE_AND_DECODE");

    char fail = 0, *d3, *s3 = "ÄãºÃWorld!"; // 6 chars
    for (int i = 0; i < 7; i++, s3++) {
        d3 = b64decode(b64encode(s3, strlen(s3)));
        if (strcmp(s3, d3)) {
            log_str(d3);
            fail++;
        }
    }
    log_num(fail);
#endif
}

int main()
{
    puts("Build time: "__TIME__);
    test();
    return 0;
}

#endif
