#include "settings.h"

namespace shared 
{

	namespace game
	{
		DWORD pid = 0;
		uintptr_t client = 0;
		uintptr_t engine = 0;
		vector2 screen_size = { 0, 0 };
	};

	namespace soundesp 
	{
		bool detected = false;
		int delay = 0;
		vector3 local_pos = { 0,0,0 };
	}

	namespace triggetbot
	{

		bool onCrosshair = false;
	}

	namespace aimbot
	{

		bool enable_temp = false;
	}

}

namespace settings
{

	namespace arduino
	{
		bool enable = false;
		std::string name = "";
	};

	namespace misc
	{
		bool only_enemies = false;
		bool only_visible = false;
		bool tg_with_ab = false;
		int bone = 0;
	};

	namespace soundesp
	{
		bool enable = false;
		std::string mode = "";
		int distance = 0;
		float frequency = 0.0f;
	};

	namespace triggerbot
	{
		bool enable = false;
		int key = 0;
		int delay_b = 0;
		int delay_a = 0;
	};

	namespace aimbot
	{
		bool enable = false;
		int key = 0;
		int fov = 0;
		float smooth = 0.0f;
	};

}