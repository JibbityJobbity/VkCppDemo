#include <iostream>

#include "Renderer.hpp"

int main(int argc, char** argv) {
	RendererConfig config{
		false,
		true,
		800,
		600
	};
	Renderer r(config);

	r.Initialize();
	r.Draw();

	return 0;
}