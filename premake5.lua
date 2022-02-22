workspace "Moon"
	configurations { "Debug", "Release" }
	architecture "x86_64"
	flags { "MultiProcessorCompile" }
	startproject "Moon"

outputdir = "%{cfg.buildcfg}-%{cfg.architecture}"

IncludeDir = {}
IncludeDir["Assimp"] = "Libs/Assimp/include"
LibraryDir = {}
LibraryDir["Assimp"] = "../Libs/Assimp/lib/assimp-vc140-mt.lib"

project "Moon"
	location "Engine"
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
	}

	defines
	{
		"_CRT_SECURE_NO_WARNINGS",
	}

	includedirs
	{
		"%{prj.name}/",
	}

	links 
	{
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
