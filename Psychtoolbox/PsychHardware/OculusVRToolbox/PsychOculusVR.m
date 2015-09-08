function varargout = PsychOculusVR(cmd, varargin)

% Global GL handle for access to OpenGL constants needed in setup:
global GL;

persistent hmd;

if nargin < 1 || isempty(cmd)
  help PsychOculusVR;
  fprintf('\n\nAlso available are functions from PsychOculusVRCore:\n');
  PsychOculusVRCore;
  return;
end

% Open a HMD:
if strcmpi(cmd, 'Open')
  handle = PsychOculusVRCore('Open', varargin{:});
  hmd{handle}.open = 1;
  varargout{1} = handle;
  return;
end

if strcmpi(cmd, 'IsOpen')
  handle = varargin{1};
  if (length(hmd) >= handle) && (handle > 0) && hmd{handle}.open
    varargout{1} = 1;
  else
    varargout{1} = 0;
  end

  return;
end

if strcmpi(cmd, 'SetupRenderingParameters')
  handle = varargin{1};

  % Query parameters for left eye view:
  [hmd{handle}.rbwidth, hmd{handle}.rbheight, vx, vy, vw, vh, ptx, pty, hsx, hsy, hsz, meshVL, meshIL, uvScale(1), uvScale(2), uvOffset(1), uvOffset(2), eyeRotStartMatrix, eyeRotEndMatrix] = PsychOculusVR ('GetFovTextureSize', handle, 0, varargin{2:end});
  %scatter(meshVL(1,:), meshVL(2,:));
  hmd{handle}.viewportLeft = [vx, vy, vw, vh];
  hmd{handle}.PixelsPerTanAngleAtCenterLeft = [ptx, pty];
  hmd{handle}.HmdToEyeViewOffsetLeft = [hsx, hsy, hsz];
  hmd{handle}.meshVerticesLeft = meshVL;
  hmd{handle}.meshIndicesLeft = meshIL;
  hmd{handle}.uvScaleLeft = uvScale;
  hmd{handle}.uvOffsetLeft = uvOffset;
  hmd{handle}.eyeRotStartMatrixLeft = eyeRotStartMatrix;
  hmd{handle}.eyeRotEndMatrixLeft = eyeRotEndMatrix;

scaleL=uvScale
offsetL=uvOffset

%rotStartL = eyeRotStartMatrix
%rotEndL = eyeRotEndMatrix


  % Query parameters for right eye view:
  [hmd{handle}.rbwidth, hmd{handle}.rbheight, vx, vy, vw, vh, ptx, pty, hsx, hsy, hsz, meshVR, meshIR, uvScale(1), uvScale(2), uvOffset(1), uvOffset(2), eyeRotStartMatrix, eyeRotEndMatrix] = PsychOculusVR ('GetFovTextureSize', handle, 1, varargin{2:end});
  %scatter(meshVR(1,:), meshVR(2,:));
  hmd{handle}.viewportRight = [vx, vy, vw, vh];
  hmd{handle}.PixelsPerTanAngleAtCenterRight = [ptx, pty];
  hmd{handle}.HmdToEyeViewOffsetRight = [hsx, hsy, hsz];
  hmd{handle}.meshVerticesRight = meshVR;
  hmd{handle}.meshIndicesRight = meshIR;
  hmd{handle}.uvScaleRight = uvScale;
  hmd{handle}.uvOffsetRight = uvOffset;
  hmd{handle}.eyeRotStartMatrixRight = eyeRotStartMatrix;
  hmd{handle}.eyeRotEndMatrixRight = eyeRotEndMatrix;

scaleR=uvScale
offsetR=uvOffset

%rotStartR = eyeRotStartMatrix
%rotEndR = eyeRotEndMatrix

  return;
end

if strcmpi(cmd, 'GetClientRenderbufferSize')
  handle = varargin{1};
  varargout{1} = [hmd{handle}.rbwidth, hmd{handle}.rbheight];
  return;
end

if strcmpi(cmd, 'PerformPostWindowOpenSetup')

  % Must have global GL constants:
  if isempty(GL)
    varargout{1} = 0;
    warning('PTB internal error in PsychOculusVR: GL struct not initialized?!?');
    return;
  end

  % Oculus device handle:
  handle = varargin{1};
  
  % Onscreen window handle:
  win = varargin{2};

  [slot shaderid blittercfg voidptr glsl] = Screen('HookFunction', win, 'Query', 'StereoCompositingBlit', 'StereoCompositingShaderAnaglyph');
  if slot == -1
    varargout{1} = 0;
    warning('Either the imaging pipeline is not enabled for given onscreen window, or it is not switched to Anaglyph stereo mode.');
    return;
  end

  if glsl == 0
    varargout{1} = 0;
    warning('Anaglyph shader is not operational for unknown reason. Sorry...');
    return;
  end

  % Remove old standard anaglyph shader:
  Screen('HookFunction', win, 'Remove', 'StereoCompositingBlit', slot);

  % Build the unwarp mesh display list within the OpenGL context of Screen():
  Screen('BeginOpenGL', win, 1);

  % Left eye setup:
  % ---------------

  % Build a display list that corresponds to the current calibration,
  % drawing the warp-mesh once, so it gets recorded in the display list:
  gldLeft = glGenLists(1);
  glNewList(gldLeft, GL.COMPILE);

  % Caution: Must *copy* the different rows with data into *separate* variables, so
  % the vertex array pointers to the different variables actually point to something
  % persistent! If we'd pass the meshVerticesLeft() subarrays directly to glTexCoordPointer
  % and friends then Octave/Matlab would just create a temporary copy of the extracted
  % rows, OpenGL would retrieve/assign pointers to those temporary copies, but then
  % at the end of a glVertexPointer/glTexCoordPointer call, those temporary copies would
  % go out of scope and Octave/Matlab would potentially garbage collect the variables again
  % *before* the call to glDrawElements permanently records the content of the variables.
  % The net results would be stale/dangling pointers, random data trash getting read from
  % memory and recorded in the display list - and thereby corrupted rendering! This hazard
  % doesn't exist within regular Octave/Matlab scripts, because the interpreter doesn't
  % deal with memory pointers. It is a unique hazard from the combination of C memory
  % pointers for OpenGL and Octave/Matlabs copy-on-write/data-sharing/garbage collection
  % behaviour. When we are at it, lets also cast the data to single() precision floating
  % point, to save some memory:
  vertexpos = single(hmd{handle}.meshVerticesLeft(1:4, :));
  texR = single(hmd{handle}.meshVerticesLeft(5:6, :));
  texG = single(hmd{handle}.meshVerticesLeft(7:8, :));
  texB = single(hmd{handle}.meshVerticesLeft(9:10, :));

  mintexxL = min(texR(1,:))
  maxtexxL = max(texR(1,:))
  mintexyL = min(texR(2,:))
  maxtexyL = max(texR(2,:))

  % vertex xy encodes 2D position from rows 1 and 2, z encodes timeWarp interpolation factors
  % from row 3 and w encodes vignette correction factors from row 4:
  glEnableClientState(GL.VERTEX_ARRAY);
  glVertexPointer(4, GL.FLOAT, 0, vertexpos);

  % Need separate texture coordinate sets for the three color channel to encode
  % channel specific color aberration correction sampling:

  % TexCoord set 0 encodes coordinates for the Red color channel:
  glClientActiveTexture(GL.TEXTURE0);
  glEnableClientState(GL.TEXTURE_COORD_ARRAY);
  glTexCoordPointer(2, GL.FLOAT, 0, texR);
  
  % TexCoord set 1 encodes coordinates for the Green color channel:
  glClientActiveTexture(GL.TEXTURE1);
  glEnableClientState(GL.TEXTURE_COORD_ARRAY);
  glTexCoordPointer(2, GL.FLOAT, 0, texG);

  % TexCoord set 2 encodes coordinates for the Blue color channel:
  glClientActiveTexture(GL.TEXTURE2);
  glEnableClientState(GL.TEXTURE_COORD_ARRAY);
  glTexCoordPointer(2, GL.FLOAT, 0, texB);

  % Draw the mesh. This records the content from all the variables persistently into
  % the display list storage, so they can be freed afterwards:
  glDrawElements(GL.TRIANGLES, length(hmd{handle}.meshIndicesLeft), GL.UNSIGNED_SHORT, uint16(hmd{handle}.meshIndicesLeft));

  % Disable stuff, so we can release or recycle the variables:
  glClientActiveTexture(GL.TEXTURE3);
  glDisableClientState(GL.TEXTURE_COORD_ARRAY);

  glClientActiveTexture(GL.TEXTURE2);
  glDisableClientState(GL.TEXTURE_COORD_ARRAY);

  glClientActiveTexture(GL.TEXTURE1);
  glDisableClientState(GL.TEXTURE_COORD_ARRAY);

  glClientActiveTexture(GL.TEXTURE0);
  glDisableClientState(GL.TEXTURE_COORD_ARRAY);

  glDisableClientState(GL.VERTEX_ARRAY);
  
  % Left eye display list done.
  glEndList;

  % Right eye setup:
  % ---------------

  % Build a display list that corresponds to the current calibration,
  % drawing the warp-mesh once, so it gets recorded in the display list:
  gldRight = glGenLists(1);
  glNewList(gldRight, GL.COMPILE);
global texR;
global texG;
global texB;
  vertexpos = single(hmd{handle}.meshVerticesRight(1:4, :));
  texR = single(hmd{handle}.meshVerticesRight(5:6, :));
  texG = single(hmd{handle}.meshVerticesRight(7:8, :));
  texB = single(hmd{handle}.meshVerticesRight(9:10, :));

  mintexxR = min(texR(1,:))
  maxtexxR = max(texR(1,:))
  mintexyR = min(texR(2,:))
  maxtexyR = max(texR(2,:))

  % vertex xy encodes 2D position from rows 1 and 2, z encodes timeWarp interpolation factors
  % from row 3 and w encodes vignette correction factors from row 4:
  glEnableClientState(GL.VERTEX_ARRAY);
  glVertexPointer(4, GL.FLOAT, 0, vertexpos);

  % Need separate texture coordinate sets for the three color channel to encode
  % channel specific color aberration correction sampling:

  % TexCoord set 0 encodes coordinates for the Red color channel:
  glClientActiveTexture(GL.TEXTURE0);
  glEnableClientState(GL.TEXTURE_COORD_ARRAY);
  glTexCoordPointer(2, GL.FLOAT, 0, texR);
  
  % TexCoord set 1 encodes coordinates for the Green color channel:
  glClientActiveTexture(GL.TEXTURE1);
  glEnableClientState(GL.TEXTURE_COORD_ARRAY);
  glTexCoordPointer(2, GL.FLOAT, 0, texG);

  % TexCoord set 2 encodes coordinates for the Blue color channel:
  glClientActiveTexture(GL.TEXTURE2);
  glEnableClientState(GL.TEXTURE_COORD_ARRAY);
  glTexCoordPointer(2, GL.FLOAT, 0, texB);

  % Draw the mesh. This records the content from all the variables persistently into
  % the display list storage, so they can be freed afterwards:
  glDrawElements(GL.TRIANGLES, length(hmd{handle}.meshIndicesRight), GL.UNSIGNED_SHORT, uint16(hmd{handle}.meshIndicesRight));

  % Disable stuff, so we can release or recycle the variables:
  glClientActiveTexture(GL.TEXTURE3);
  glDisableClientState(GL.TEXTURE_COORD_ARRAY);

  glClientActiveTexture(GL.TEXTURE2);
  glDisableClientState(GL.TEXTURE_COORD_ARRAY);

  glClientActiveTexture(GL.TEXTURE1);
  glDisableClientState(GL.TEXTURE_COORD_ARRAY);

  glClientActiveTexture(GL.TEXTURE0);
  glDisableClientState(GL.TEXTURE_COORD_ARRAY);

  glDisableClientState(GL.VERTEX_ARRAY);
  
  % Right eye display list done.
  glEndList;

  Screen('EndOpenGL', win);

  texwidth = RectWidth(Screen('Rect', win, 1));
  texheight = RectHeight(Screen('Rect', win, 1));
  
  % Setup left eye shader:
  glsl = LoadGLSLProgramFromFiles('OculusRiftCorrectionShader');
  glUseProgram(glsl);
  glUniform1i(glGetUniformLocation(glsl, 'Image'), 0);
  glUniform2f(glGetUniformLocation(glsl, 'EyeToSourceUVOffset'), hmd{handle}.uvOffsetLeft(1) * texwidth, hmd{handle}.uvOffsetLeft(2) * texheight);
  %glUniform2f(glGetUniformLocation(glsl, 'EyeToSourceUVOffset'), texwidth/2, texheight/2);
  glUniform2f(glGetUniformLocation(glsl, 'EyeToSourceUVScale'), hmd{handle}.uvScaleLeft(1) * texwidth, hmd{handle}.uvScaleLeft(2) * texheight);
  %glUniform2f(glGetUniformLocation(glsl, 'EyeToSourceUVScale'), texwidth/2, texheight/2);
  glUniformMatrix4fv(glGetUniformLocation(glsl, 'EyeRotationStart'), 1, 1, diag([1 1 1 1]));
  glUniformMatrix4fv(glGetUniformLocation(glsl, 'EyeRotationEnd'), 1, 1, diag([1 1 1 1]));
  %glUniformMatrix4fv(glGetUniformLocation(glsl, 'EyeRotationStart'), 1, 1, hmd{handle}.eyeRotStartMatrixLeft);
  %glUniformMatrix4fv(glGetUniformLocation(glsl, 'EyeRotationEnd'), 1, 1, hmd{handle}.eyeRotEndMatrixLeft);
  glUseProgram(0);

  % Insert it at former position of the old shader:
  posstring = sprintf('InsertAt%iShader', slot);
  
  % xOffset and yOffset encode the viewport location and size for the left-eye vs.
  % right eye view in the shared output window - or the source renderbuffer if both eyes
  % would be rendered into a shared texture. However, the meshes provided by the SDK
  % already encode proper left and right offsets for output, and the inputs are separate
  % textures for left and right eye, so using the offset is not needed. Also our correction
  % shader ignores the modelview matrix which would get updated with the "Offset:%i%i" blittercfg,
  % instead is takes normalized device coordinates NDC directly from the distortion mesh. Iow, not
  % only is xOffset/yOffset not needed, it would also be a no operation due to our specific shader.
  % We leave this here for documentation for now, in case we need to change our ways of doing this.
  leftViewPort = hmd{handle}.viewportLeft
  % xOffset = hmd{handle}.viewportLeft(1) % Viewport x start.
  % yOffset = hmd{handle}.viewportLeft(2) % Viewport y start.
  % blittercfg = sprintf('Blitter:DisplayListBlit:Handle:%i:Bilinear:Offset:%i:%i', gldLeft, xOffset, yOffset);
  blittercfg = sprintf('Blitter:DisplayListBlit:Handle:%i:Bilinear', gldLeft);
  Screen('Hookfunction', win, posstring, 'StereoCompositingBlit', 'OculusVRClientCompositingShaderLeftEye', glsl, blittercfg);

  % Setup right eye shader:
  glsl = LoadGLSLProgramFromFiles('OculusRiftCorrectionShader');
  glUseProgram(glsl);
  glUniform1i(glGetUniformLocation(glsl, 'Image'), 1);
  glUniform2f(glGetUniformLocation(glsl, 'EyeToSourceUVOffset'), hmd{handle}.uvOffsetRight(1) * texwidth, hmd{handle}.uvOffsetRight(2) * texheight);
  %glUniform2f(glGetUniformLocation(glsl, 'EyeToSourceUVOffset'), texwidth/2, texheight/2);
  glUniform2f(glGetUniformLocation(glsl, 'EyeToSourceUVScale'), hmd{handle}.uvScaleRight(1) * texwidth, hmd{handle}.uvScaleRight(2) * texheight);
  %glUniform2f(glGetUniformLocation(glsl, 'EyeToSourceUVScale'), texwidth/2, texheight/2);

  glUniformMatrix4fv(glGetUniformLocation(glsl, 'EyeRotationStart'), 1, 1, diag([1 1 1 1]));
  glUniformMatrix4fv(glGetUniformLocation(glsl, 'EyeRotationEnd'), 1, 1, diag([1 1 1 1]));
  %glUniformMatrix4fv(glGetUniformLocation(glsl, 'EyeRotationStart'), 1, 1, hmd{handle}.eyeRotStartMatrixRight);
  %glUniformMatrix4fv(glGetUniformLocation(glsl, 'EyeRotationEnd'), 1, 1, hmd{handle}.eyeRotEndMatrixRight);
  glUseProgram(0);

  % Insert it at former position of the old shader:
  posstring = sprintf('InsertAt%iShader', slot);
  % See above for why xOffset/yOffset is not used here.
  % xOffset = hmd{handle}.viewportRight(1) % Viewport x start.
  % yOffset = hmd{handle}.viewportRight(2) % Viewport y start.
  % blittercfg = sprintf('Blitter:DisplayListBlit:Handle:%i:Bilinear:Offset:%i:%i', gldRight, xOffset, yOffset);
  blittercfg = sprintf('Blitter:DisplayListBlit:Handle:%i:Bilinear', gldRight);
  Screen('Hookfunction', win, posstring, 'StereoCompositingBlit', 'OculusVRClientCompositingShaderRightEye', glsl, blittercfg);

  % Return success result code 1:
  varargout{1} = 1;
  return;
end

% 'cmd' so far not dispatched? Let's assume it is a command
% meant for PsychOculusVRCore:
[ varargout{1:nargout} ] = PsychOculusVRCore(cmd, varargin{:});
return;

end