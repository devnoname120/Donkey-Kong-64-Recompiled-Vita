// Host-only observations of calls actually submitted to GLES. They never enter
// the Vita executable and are not a frame-time benchmark.
#include "gl_audit.h"
#include <GLES2/gl2.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

namespace {
    struct Counters {
        std::atomic<uint64_t> uniformCalls{0}, ignoredUniformCalls{0}, repeatedUniformCalls{0}, draws{0};
    } counts;
    GLuint program=0;
    std::map<std::pair<GLuint,GLint>,std::vector<uint8_t>> values;
    void uniform(GLint location,const void *data,size_t bytes) {
        ++counts.uniformCalls;
        if(location==-1) { ++counts.ignoredUniformCalls; return; }
        auto &previous=values[{program,location}];
        if(previous.size()==bytes && !std::memcmp(previous.data(),data,bytes)) ++counts.repeatedUniformCalls;
        previous.assign(static_cast<const uint8_t *>(data),static_cast<const uint8_t *>(data)+bytes);
    }
}
ProbeGLStats probeGLStats() { return {counts.uniformCalls.load(),counts.ignoredUniformCalls.load(),counts.repeatedUniformCalls.load(),counts.draws.load()}; }
void resetProbeGLStats() { counts.uniformCalls=0; counts.ignoredUniformCalls=0; counts.repeatedUniformCalls=0; counts.draws=0; }
void reportProbeGLStats() {
    const auto snapshot=probeGLStats();
    std::fprintf(stderr,"PROBE GL: draws=%llu uniforms=%llu ignored_uniforms=%llu repeated_uniforms=%llu\n",
        static_cast<unsigned long long>(snapshot.draws),static_cast<unsigned long long>(snapshot.uniformCalls),
        static_cast<unsigned long long>(snapshot.ignoredUniformCalls),static_cast<unsigned long long>(snapshot.repeatedUniformCalls));
}
extern "C" {
void __real_glUseProgram(GLuint);
void __wrap_glUseProgram(GLuint value) { program=value; __real_glUseProgram(value); }
void __real_glDeleteProgram(GLuint);
void __wrap_glDeleteProgram(GLuint value) {
    for(auto it=values.begin();it!=values.end();) {
        if(it->first.first==value) it=values.erase(it); else ++it;
    }
    __real_glDeleteProgram(value);
}
void __real_glUniform1i(GLint,GLint);
void __wrap_glUniform1i(GLint location,GLint x) { uniform(location,&x,sizeof(x)); __real_glUniform1i(location,x); }
void __real_glUniform1f(GLint,GLfloat);
void __wrap_glUniform1f(GLint location,GLfloat x) { uniform(location,&x,sizeof(x)); __real_glUniform1f(location,x); }
void __real_glUniform2f(GLint,GLfloat,GLfloat);
void __wrap_glUniform2f(GLint location,GLfloat x,GLfloat y) { const GLfloat v[]={x,y}; uniform(location,v,sizeof(v)); __real_glUniform2f(location,x,y); }
void __real_glUniform3f(GLint,GLfloat,GLfloat,GLfloat);
void __wrap_glUniform3f(GLint location,GLfloat x,GLfloat y,GLfloat z) { const GLfloat v[]={x,y,z}; uniform(location,v,sizeof(v)); __real_glUniform3f(location,x,y,z); }
void __real_glUniform4f(GLint,GLfloat,GLfloat,GLfloat,GLfloat);
void __wrap_glUniform4f(GLint location,GLfloat x,GLfloat y,GLfloat z,GLfloat w) { const GLfloat v[]={x,y,z,w}; uniform(location,v,sizeof(v)); __real_glUniform4f(location,x,y,z,w); }
void __real_glUniform3fv(GLint,GLsizei,const GLfloat *);
void __wrap_glUniform3fv(GLint location,GLsizei count,const GLfloat *v) { uniform(location,v,size_t(count)*3*sizeof(*v)); __real_glUniform3fv(location,count,v); }
void __real_glUniform4fv(GLint,GLsizei,const GLfloat *);
void __wrap_glUniform4fv(GLint location,GLsizei count,const GLfloat *v) { uniform(location,v,size_t(count)*4*sizeof(*v)); __real_glUniform4fv(location,count,v); }
void __real_glDrawArrays(GLenum,GLint,GLsizei);
void __wrap_glDrawArrays(GLenum mode,GLint first,GLsizei count) { ++counts.draws; __real_glDrawArrays(mode,first,count); }
}
