workspace "Moon"
	configurations { "Debug", "Release" }
	architecture "x86_64"
	flags { "MultiProcessorCompile" }
	startproject "Moon"

outputdir = "%{cfg.buildcfg}-%{cfg.architecture}"

VULKAN_SDK = os.getenv("VULKAN_SDK")

IncludeDir = {}
IncludeDir["Assimp"] = "Libs/Assimp/include"
IncludeDir["glfw"] = "Libs/glfw/include"
IncludeDir["glm"] = "Libs/glm/"
IncludeDir["imgui"] = "Libs/imgui"
IncludeDir["stb"] = "Libs/stb"
IncludeDir["VulkanSDK"] = "%{VULKAN_SDK}/Include"

LibraryDir = {}
LibraryDir["Assimp"] = "../Libs/Assimp/lib/assimp-vc140-mt.lib"
LibraryDir["VulkanSDK"] = "%{VULKAN_SDK}/Lib/vulkan-1.lib"

group "Dependencies"
	include "Libs/imgui"
	include "Libs/glfw"
group ""

project "Moon"
	location "Moon"
	kind "ConsoleApp"
	language "C++"
	cppdialect "C++17"
	staticruntime "on"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("obj/" .. outputdir .. "/%{prj.name}")

	files
	{
		"%{prj.name}/src/**.h",
		"%{prj.name}/src/**.cpp",
		"Libs/stb_image/**.h",
		"Libs/stb_image/**.cpp",
		"Libs/glm/glm/**.hpp",
		"Libs/glm/glm/**.inl",
	}

	defines
	{
		"_CRT_SECURE_NO_WARNINGS",
		"GLFW_INCLUDE_NONE"
	}

	includedirs
	{
		"%{prj.name}/src",
		"%{IncludeDir.Assimp}",
		"%{IncludeDir.glfw}",
		"%{IncludeDir.imgui}",
		"%{IncludeDir.glm}",
		"%{IncludeDir.stb}",
		"%{IncludeDir.VulkanSDK}"
	}

	links 
	{
		"glfw",
		"imgui",
		"%{LibraryDir.VulkanSDK}"
	}

	postbuildcommands 
	{
	}

	filter "system:windows"
	systemversion "latest"

	defines
	{
	}

	filter "configurations:Debug"
		defines {"DEBUG", "_DEBUG", "MN_DEBUG"}
		runtime "Debug"
		symbols "on"

	filter "configurations:Release"
		defines {"NDEBUG","_RELEASE", "MN_RELEASE"}
		runtime "Release"
		optimize "on"
