require "gl"
local VLC = require "vlcwrp"

-- assert(os.getenv("LRUN_HOME"), "LRUN_HOME environment var is not defined")
assert(arg[1], "expected argument media url")

_width, _height = 800, 600
fullscreen = false
lighting = false
wired = true
rotating = true
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

-- white diffuse light.
local light_diffuse = {1.0, 1.0, 1.0, 1.0}

-- infinite light location.
local light_position = {-1.0, 1.0, 2.0, 0.0}

-- normals for the 6 faces of a cube.
local normals = {
	{-1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0},
	{0.0, -1.0, 0.0}, {0.0, 0.0, 1.0}, {0.0, 0.0, -1.0}
}

-- vertex indices for the 6 faces of a cube.
local faces = {
	{1, 2, 3, 4}, {4, 3, 7, 8}, {8, 7, 6, 5},
	{5, 6, 2, 1}, {6, 7, 3, 2}, {8, 5, 1, 4}
}

local cubemap = {true,true,false,true,true,false}

-- cube vertex data.  
local vertices = {
	{-1, -1, 1},
	{-1, -1, -1},
	{-1,  1, -1},
	{-1,  1,  1},
	{ 1,  -1,  1},
	{ 1,  -1,  -1},
	{ 1,  1,  -1},
	{ 1,  1,  1},
}

function drawBox()
	gl.Enable("POINT_SMOOTH")
	gl.Enable("LINE_SMOOTH")
	gl.Hint("LINE_SMOOTH", "NICEST")
	gl.Hint("POINT_SMOOTH", "NICEST")
	for i=1,6 do
		if cubemap[i] then
			gl.Enable("TEXTURE_2D")
			gl.Begin("QUADS")
			gl.Normal(normals[i])
			gl.TexCoord(1.0, 1.0); gl.Vertex(unpack(vertices[faces[i][1]]))
			gl.TexCoord(0.0, 1.0); gl.Vertex(unpack(vertices[faces[i][2]]))
			gl.TexCoord(0.0, 0.0); gl.Vertex(unpack(vertices[faces[i][3]]))
			gl.TexCoord(1.0, 0.0); gl.Vertex(unpack(vertices[faces[i][4]]))
		else
			if wired then
				gl.Begin("LINE_LOOP")
			else
				gl.Begin("QUADS")
				gl.Normal(normals[i])
			end
			for j=1,4 do
				gl.Color(1, 1, 1)
				gl.Vertex(unpack(vertices[faces[i][j]]))
			end
		end
		gl.End()
		gl.Disable("TEXTURE_2D")
	end
	gl.Disable("LINE_SMOOTH")
end

function reshape(width, height)
	gl.Viewport(0, 0, width, height)
    --glu.LookAt(0.0, 0.0, 1, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0)
    --glu.Perspective(90, 1, -1.0, 1.0)
end

function display()
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

	if rotating then
		gl.Rotate(0.3, .5, 1, 1)
	end
	gl.Clear("COLOR_BUFFER_BIT,DEPTH_BUFFER_BIT")
	gl.PushMatrix()
	drawBox()
	gl.PopMatrix()
	gl.Flush()
	glut.SwapBuffers()
end

function setlights()
	if lighting then
		gl.Enable("LIGHT0")
		gl.Enable("LIGHTING")
	else
		gl.Disable("LIGHT0")
		gl.Disable("LIGHTING")
	end
end

function init(mrl)
	-- Enable a single OpenGL light.  
	gl.Light("LIGHT0", "DIFFUSE", light_diffuse)
	gl.Light("LIGHT0", "POSITION", light_position)
	setlights()

	-- Use depth buffering for hidden surface elimination.  
	gl.Enable("DEPTH_TEST")

	-- Setup the view of the cube.  
	gl.MatrixMode("PROJECTION")
	glu.Perspective( 40.0, -- field of view in degree   
		1.0, -- aspect ratio   
		1.0, -- Z near  
		10.0 -- Z far  
	)
	gl.MatrixMode("MODELVIEW")
	glu.LookAt(0.0, 0.0, 5.0,  -- eye is at (0,0,5)  
		0.0, 0.0, 0.0,      -- center is at (0,0,0)  
		0.0, 1.0, 0.0)      -- up is in positive Y direction  

	-- Adjust cube position to be asthetic angle.  
	gl.Translate(0.0, 0.0, -1.0)
	gl.Rotate(30, 1.0, 0.0, 0.0)
	gl.Rotate(-20, 0.0, 0.0, 1.0)

	-- init video
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
	gl.TexEnv("TEXTURE_ENV_MODE", "DECALL")

	-- alpha blending
	gl.Enable("BLEND")
	gl.BlendFunc("SRC_ALPHA", "ONE_MINUS_SRC_ALPHA")
end

function timer()
  display()
  glut.TimerFunc(5, "timer")
end

function keyboard(key)
	if key == 27 then
		os.exit()
	elseif key == string.byte('f') then
		if fullscreen then
			glut.ReshapeWindow(WIDTH, HEIGHT)
		else
			skip_reshape = true
			glut.FullScreen()
		end
		fullscreen = not fullscreen
	elseif key == string.byte('w') then
		wired = not wired
	elseif key == string.byte('l') then
		lighting = not lighting
		setlights()
	elseif key >= string.byte('1') and key <= string.byte('6') then
		local face = 1 + key - string.byte('1')
		cubemap[face] = not cubemap[face]
	elseif key == string.byte(' ') then
		rotating = not rotating
	end
end

glut.Init()
glut.InitDisplayMode("DOUBLE,RGB,DEPTH")
glut.InitWindowSize(WIN_WIDTH, WIN_HEIGHT)
glut.CreateWindow("VLC 3D")
if fullscreen then
	glut.FullScreen()
end
glut.DisplayFunc("display")
glut.ReshapeFunc('reshape')
glut.KeyboardFunc('keyboard')
glut.TimerFunc(5, "timer")
init(arg[1])
glut.MainLoop()
