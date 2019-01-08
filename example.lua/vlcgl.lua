-----------------------------------------------------------------------
--                                                                   --
-- Copyright (C) 2009,  AVIQ Bulgaria Ltd.                           --
--                                                                   --
-- Project:       VLC/OpenGL example                                 --
-- Filename:      vlcgl.lua                                          --
--                                                                   --
-----------------------------------------------------------------------

-- assert(os.getenv("LRUN_HOME"), "LRUN_HOME environment var is not defined")
assert(arg[1], "expected argument media url")

require "gl"
local VLC = require "vlcwrp"

-- maximum display frame rate
DISPLAY_FREQUENCY_FPS = 30
TIMEOUT = 1000/DISPLAY_FREQUENCY_FPS
WIN_WIDTH, WIN_HEIGHT = 800, 600
WIDTH, HEIGHT  = 800, 600

local function pathsep(p)
	if os.getenv("OS") == "Windows_NT" then
		p = string.gsub(p, "/", "\\")
	else
		p = string.gsub(p, "\\", "/")
	end
	return p
end

local vlc = nil
local tid = nil
local ndisplays = 0
local timeStart = 0

function Init(mrl)
	print("Initializing ", mrl)
	vpl = VLC.new
	{
		"-q",
		"--ignore-config",
		--"--plugin-path", pathsep(os.getenv("LRUN_HOME").."/lib/vlc/plugins"),
		--"--codec", "avcodec",
		--"--ffmpeg-hw",
		vmem_width = WIDTH,
		vmem_height = HEIGHT,
	}
	vpl:play(mrl)

	gl.Enable("TEXTURE_2D")

	tid = gl.GenTextures(1)[1]

	-- select our current texture
	gl.BindTexture( "TEXTURE_2D", tid )
    gl.TexParameter( "TEXTURE_2D", "TEXTURE_PRIORITY", 1.0 )

	-- the texture wraps over at the edges (repeat)
	gl.TexParameter( "TEXTURE_2D", "TEXTURE_WRAP_S", "CLAMP" )
	gl.TexParameter( "TEXTURE_2D", "TEXTURE_WRAP_T", "CLAMP" )

	-- when texture area is small, bilinear filter the closest mipmap
	gl.TexParameter( "TEXTURE_2D", "TEXTURE_MIN_FILTER", "LINEAR" )
	--gl.TexParameter( "TEXTURE_2D", "TEXTURE_MAG_FILTER", "LINEAR" )
	gl.TexParameter( "TEXTURE_2D", "TEXTURE_MAG_FILTER", "LINEAR" )
	--gl.TexParameter( "TEXTURE_2D", "TEXTURE_MIN_FILTER", "LINEAR" )

	-- select modulate to mix texture with color for shading
	gl.TexEnv("TEXTURE_ENV_MODE", "DECAL")
	
	gl.Disable("TEXTURE_2D")

    --gl.Disable("BLEND")
	gl.Enable("BLEND")
	gl.BlendFunc("SRC_ALPHA", "ONE_MINUS_SRC_ALPHA")

    gl.Disable("DEPTH_TEST")
    gl.DepthMask(false)
    gl.Disable("CULL_FACE")
    gl.ClearColor( 0.0, 0.0, 0.0, 1.0 )
    gl.Clear( "COLOR_BUFFER_BIT" )
end

function Done()
end

function Reshape(width, height)
	gl.Viewport(0, 0, width, height)
	gl.MatrixMode('PROJECTION')
	gl.LoadIdentity()
	gl.MatrixMode('MODELVIEW')
	gl.LoadIdentity()
	gl.Ortho(-1, 1, -1, 1, -1.0, 1.0)
    --glu.LookAt(0.0, 0.0, 1, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0)
    --glu.Perspective(90, 1, -1.0, 1.0)
end

function DrawFrame()
	gl.Enable("TEXTURE_2D")
	gl.BindTexture( "TEXTURE_2D", tid )

	local frame = vpl:frame_acquire()
	if frame then
		if not once then
			gl.TexImageS(0, 4, "RGBA", HEIGHT, frame, WIDTH*HEIGHT*4)
			once = true
		else
			gl.TexSubImageS(0, "RGBA", HEIGHT, frame, WIDTH*HEIGHT*4)
		end
	end
	vpl:frame_release()

	gl.Begin("QUADS")
	gl.TexCoord(0.0, 0.0); gl.Vertex(-1.0, 1.0)
	gl.TexCoord(1.0, 0.0); gl.Vertex(1.0, 1.0)
	gl.TexCoord(1.0, 1.0); gl.Vertex(1.0, -1.0)
	gl.TexCoord(0.0, 1.0); gl.Vertex(-1.0, -1.0)
	gl.End()

	gl.Disable("TEXTURE_2D")

	gl.Flush()
	glut.SwapBuffers()

	if timeStart == 0 then
		timeStart = glut.Get("ELAPSED_TIME")
	end
	ndisplays = ndisplays + 1
	io.write(string.format("FPS=%.2f                  \r", 1000/((glut.Get("ELAPSED_TIME") - timeStart) / ndisplays))) io.flush()
end

function Timer()
	local displayStart = glut.Get("ELAPSED_TIME")
	DrawFrame()
	local displayTime = glut.Get("ELAPSED_TIME") - displayStart
	if displayTime < TIMEOUT then
		glut.TimerFunc(TIMEOUT - displayTime, "Timer")
	else
		Timer()
	end
end

function Keyboard(key)
	if key == 27 then
		os.exit()
	end
end

function Mouse(button, state, x, y)
	if button == "left" and state then
		vpl:play(arg[1])
	elseif button == "right" and state then
		vpl:play(arg[2])
	end
end

glut.Init()
glut.InitDisplayMode()
glut.InitWindowSize(WIN_WIDTH, WIN_HEIGHT)
glut.CreateWindow("VLC/OpenGL")
glut.DisplayFunc('DrawFrame')
glut.ReshapeFunc('Reshape')
glut.KeyboardFunc('Keyboard')
glut.MouseFunc('Mouse')
glut.TimerFunc(0, 'Timer')
Init(arg[1])

glut.MainLoop()
