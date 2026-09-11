// The GL3 renderer is not part of the server, but the scene graph's
// rendermodelext_drawable.h references one GLWrapper method; the null GL
// layer makes it a no-op.
#include "graphics/gl3v/glwrapper.h"

void GLWrapper::drawGeometry(GLuint vao, GLuint elementCount)
{
	(void)vao;
	(void)elementCount;
}
