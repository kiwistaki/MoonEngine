#pragma once
#include "RenderTypes.h"
#include <SDL_events.h>

namespace Moon
{
	class Camera
	{
	public:
		glm::mat4 getViewMatrix();
		glm::mat4 getRotationMatrix();

		void processSDLEvent(SDL_Event& e);

		void update();

	public:
		glm::vec3 velocity;
		glm::vec3 position;
		float pitch{ 0.f };
		float yaw{ 0.f };

		bool processMouseMotion = false;
	};
}