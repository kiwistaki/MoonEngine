#include <RenderDevice.h>

int main(int argc, char* argv[])
{
	Moon::RenderDevice engine;
	engine.init();		
	engine.run();	
	engine.cleanup();	
	return 0;
}
