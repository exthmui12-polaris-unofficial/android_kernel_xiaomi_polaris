/* SPDX-License-Identifier: GPL-2.0 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s EGL=%x GL=%x\n", __LINE__, #x, eglGetError(), glGetError()); return 1; } } while (0)

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static GLuint shader(GLenum kind, const char *source)
{
    GLuint object = glCreateShader(kind);
    glShaderSource(object, 1, &source, NULL);
    glCompileShader(object);
    GLint ok = 0;
    glGetShaderiv(object, GL_COMPILE_STATUS, &ok);
    if (!ok) { glDeleteShader(object); return 0; }
    return object;
}

int main(int argc, char **argv)
{
    int seconds = argc == 2 ? atoi(argv[1]) : 600;
    if (seconds < 1 || seconds > 600) return 2;
    setbuf(stdout, NULL);
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    CHECK(display != EGL_NO_DISPLAY && eglInitialize(display, NULL, NULL));
    EGLint attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
        EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config;
    EGLint count;
    CHECK(eglChooseConfig(display, attrs, &config, 1, &count) && count == 1);
    EGLint surface_attrs[] = {EGL_WIDTH, 1024, EGL_HEIGHT, 1024, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, surface_attrs);
    CHECK(surface != EGL_NO_SURFACE);
    EGLint context_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attrs);
    CHECK(context != EGL_NO_CONTEXT && eglMakeCurrent(display, surface, surface, context));
    printf("pid=%d renderer=%s version=%s duration=%d\n", getpid(), glGetString(GL_RENDERER), glGetString(GL_VERSION), seconds);
    GLuint vertex = shader(GL_VERTEX_SHADER, "attribute vec2 pos; void main(){gl_Position=vec4(pos,0.,1.);}");
    GLuint fragment = shader(GL_FRAGMENT_SHADER, "precision mediump float; uniform vec4 color; void main(){gl_FragColor=color;}");
    CHECK(vertex && fragment);
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex); glAttachShader(program, fragment);
    glBindAttribLocation(program, 0, "pos"); glLinkProgram(program);
    GLint linked; glGetProgramiv(program, GL_LINK_STATUS, &linked); CHECK(linked);
    glUseProgram(program);
    GLint color = glGetUniformLocation(program, "color"); CHECK(color >= 0);
    GLfloat triangle[] = {-1,-1, 3,-1, -1,3};
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, triangle);
    glEnableVertexAttribArray(0); glViewport(0,0,1024,1024);
    unsigned long frames = 0, last_frames = 0;
    double start=now(), last=start, max_frame=0;
    while (now() - start < seconds) {
        double frame_start=now();
        int red=(frames & 1) != 0;
        glUniform4f(color, red ? 1.f : 0.f, 0.f, red ? 0.f : 1.f, 1.f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        unsigned char pixel[4];
        glReadPixels(512,512,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        CHECK(glGetError()==GL_NO_ERROR);
        CHECK(pixel[red ? 0 : 2] >= 250 && pixel[red ? 2 : 0] <= 5 && pixel[1] <= 5);
        frames++;
        double elapsed=now()-frame_start;
        if (elapsed > max_frame) max_frame=elapsed;
        if (now()-last >= 30) {
            printf("elapsed=%.1f frames=%lu fps=%.1f max_frame_ms=%.2f pixels=PASS\n",
                now()-start,frames,(frames-last_frames)/(now()-last),max_frame*1000);
            last=now(); last_frames=frames;
        }
        usleep(16000);
    }
    glDeleteProgram(program); glDeleteShader(vertex); glDeleteShader(fragment);
    CHECK(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));
    CHECK(eglDestroyContext(display,context)); CHECK(eglDestroySurface(display,surface));
    CHECK(eglTerminate(display));
    printf("PASS duration=%.1f frames=%lu pixel_checks=%lu\n",now()-start,frames,frames);
    return 0;
}
