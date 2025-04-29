import sys
import os
import tkinter as tk
from tkinter import filedialog
import numpy as np
import glfw
from OpenGL.GL import *
from OpenGL.GLU import *
from PIL import Image  # for texture loading

# global variables
vertices = []
faces = []
normals = []
texcoords = []
rotation = [0, 0]
zoom = 1.0
last_x, last_y = 0, 0
left_mouse_pressed = False
model_center = [0, 0, 0]
model_scale = 1.0

# material related variables
materials = {}
current_material = None
texture_ids = {}

def load_mtl(mtl_path):
    # load material definitions
    global materials
    materials = {}
    current_mtl = None
    
    # get the directory of the mtl file for relative texture paths
    mtl_dir = os.path.dirname(mtl_path)
    
    try:
        with open(mtl_path, 'r') as mtl_file:
            for line in mtl_file:
                if line.startswith('#') or not line.strip():
                    continue
                    
                values = line.split()
                if not values:
                    continue
                
                if values[0] == 'newmtl':
                    # start a new material
                    current_mtl = values[1]
                    materials[current_mtl] = {
                        'Ka': [0.2, 0.2, 0.2],  # ambient color
                        'Kd': [0.8, 0.8, 0.8],  # diffuse color
                        'Ks': [0.0, 0.0, 0.0],  # specular color
                        'Ns': 0.0,              # specular exponent
                        'd': 1.0,               # transparency
                        'map_Kd': None,         # diffuse texture
                    }
                elif current_mtl is not None:
                    if values[0] == 'Ka' and len(values) >= 4:
                        materials[current_mtl]['Ka'] = [float(values[1]), float(values[2]), float(values[3])]
                    elif values[0] == 'Kd' and len(values) >= 4:
                        materials[current_mtl]['Kd'] = [float(values[1]), float(values[2]), float(values[3])]
                    elif values[0] == 'Ks' and len(values) >= 4:
                        materials[current_mtl]['Ks'] = [float(values[1]), float(values[2]), float(values[3])]
                    elif values[0] == 'Ns' and len(values) >= 2:
                        materials[current_mtl]['Ns'] = float(values[1])
                    elif values[0] == 'd' and len(values) >= 2:
                        materials[current_mtl]['d'] = float(values[1])
                    elif values[0] == 'map_Kd' and len(values) >= 2:
                        # store absolute path to texture
                        tex_path = values[1]
                        if not os.path.isabs(tex_path):
                            tex_path = os.path.join(mtl_dir, tex_path)
                        materials[current_mtl]['map_Kd'] = tex_path
    except Exception as e:
        print(f"Error loading MTL file: {e}")

def load_textures():
    # load all textures from materials
    global materials, texture_ids
    texture_ids = {}
    
    for mtl_name, mtl in materials.items():
        if mtl['map_Kd'] and os.path.exists(mtl['map_Kd']):
            try:
                # generate texture id
                texture_id = glGenTextures(1)
                texture_ids[mtl_name] = texture_id
                
                # load image with PIL
                image = Image.open(mtl['map_Kd'])
                image_data = image.convert("RGBA").tobytes()
                
                # bind and setup texture
                glBindTexture(GL_TEXTURE_2D, texture_id)
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR)
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR)
                
                width, height = image.size
                glTexImage2D(
                    GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, 
                    GL_RGBA, GL_UNSIGNED_BYTE, image_data
                )
            except Exception as e:
                print(f"Error loading texture '{mtl['map_Kd']}': {e}")

def load_obj(file_path):
    # reset arrays
    global vertices, faces, normals, texcoords, model_center, model_scale
    global materials, current_material
    vertices = []
    faces = []
    normals = []
    texcoords = []
    face_materials = []  # to store material for each face
    mtl_file = None
    current_material = None
    
    # get the directory of the obj file for relative mtl paths
    obj_dir = os.path.dirname(file_path)
    
    # open and read the obj file
    with open(file_path, 'r') as file:
        for line in file:
            if line.startswith('#'):  # ignore comments
                continue
                
            values = line.split()
            if not values:
                continue
            
            if values[0] == 'mtllib' and len(values) >= 2:
                # load material library
                mtl_path = values[1]
                if not os.path.isabs(mtl_path):
                    mtl_path = os.path.join(obj_dir, mtl_path)
                if os.path.exists(mtl_path):
                    load_mtl(mtl_path)
                    
            elif values[0] == 'usemtl' and len(values) >= 2:
                # set current material
                current_material = values[1]
                
            elif values[0] == 'v':  # vertex
                vertices.append([float(values[1]), float(values[2]), float(values[3])])
            elif values[0] == 'vn':  # normal
                normals.append([float(values[1]), float(values[2]), float(values[3])])
            elif values[0] == 'vt':  # texture coordinate
                if len(values) >= 3:
                    texcoords.append([float(values[1]), float(values[2])])
                else:
                    texcoords.append([float(values[1]), 0.0])
            elif values[0] == 'f':  # face
                face = []
                face_vts = []
                face_vns = []
                
                for v in values[1:]:
                    # handle different face formats
                    w = v.split('/')
                    vertex_idx = int(w[0]) - 1  # obj indices start at 1
                    face.append(vertex_idx)
                    
                    # add texture and normal indices if available
                    if len(w) > 1 and w[1]:
                        texcoord_idx = int(w[1]) - 1
                        face_vts.append(texcoord_idx)
                    else:
                        face_vts.append(-1)
                        
                    if len(w) > 2 and w[2]:
                        normal_idx = int(w[2]) - 1
                        face_vns.append(normal_idx)
                    else:
                        face_vns.append(-1)
                
                faces.append((face, face_vts, face_vns))
                face_materials.append(current_material)
    
    # convert to numpy arrays for performance
    vertices = np.array(vertices, dtype=np.float32)
    if normals:
        normals = np.array(normals, dtype=np.float32)
    if texcoords:
        texcoords = np.array(texcoords, dtype=np.float32)
    
    # center and scale the model
    if len(vertices) > 0:
        # find center and scale
        mins = np.min(vertices, axis=0)
        maxs = np.max(vertices, axis=0)
        model_center = (mins + maxs) / 2
        
        # calculate scale to fit in view
        model_scale = np.max(maxs - mins) / 2
        if model_scale == 0:
            model_scale = 1.0  # prevent division by zero
    
    # load textures after model loading
    if materials:
        load_textures()
    
    # store face materials for rendering
    return face_materials

def mouse_button_callback(window, button, action, mods):
    # handle mouse clicks
    global left_mouse_pressed, last_x, last_y
    
    if button == glfw.MOUSE_BUTTON_LEFT:
        if action == glfw.PRESS:
            left_mouse_pressed = True
            last_x, last_y = glfw.get_cursor_pos(window)
        elif action == glfw.RELEASE:
            left_mouse_pressed = False

def cursor_position_callback(window, xpos, ypos):
    # handle mouse movement for rotation
    global left_mouse_pressed, last_x, last_y, rotation
    
    if left_mouse_pressed:
        dx = xpos - last_x
        dy = ypos - last_y
        
        # update rotation
        rotation[0] += dy * 0.5
        rotation[1] += dx * 0.5
        
        last_x, last_y = xpos, ypos

def scroll_callback(window, xoffset, yoffset):
    # handle scrolling for zoom
    global zoom
    
    # adjust zoom with scroll wheel
    zoom *= 1.1 ** yoffset

def key_callback(window, key, scancode, action, mods):
    # handle keyboard input
    if key == glfw.KEY_ESCAPE and action == glfw.PRESS:
        glfw.set_window_should_close(window, True)

def apply_material(mtl_name):
    # apply material properties to opengl
    if mtl_name is None or mtl_name not in materials:
        # use default material if none specified
        glDisable(GL_TEXTURE_2D)
        glColor3f(0.8, 0.8, 0.8)  # default light gray
        return
    
    mtl = materials[mtl_name]
    
    # set material colors
    glMaterialfv(GL_FRONT, GL_AMBIENT, mtl['Ka'] + [1.0])
    glMaterialfv(GL_FRONT, GL_DIFFUSE, mtl['Kd'] + [mtl['d']])
    glMaterialfv(GL_FRONT, GL_SPECULAR, mtl['Ks'] + [1.0])
    
    # clamp shininess to valid range (0-128) to avoid OpenGL errors
    shininess = min(max(mtl['Ns'], 0.0), 128.0)
    glMaterialf(GL_FRONT, GL_SHININESS, shininess)
    
    # basic color without lighting
    glColor3fv(mtl['Kd'])
    
    # handle transparency
    if mtl['d'] < 1.0:
        glEnable(GL_BLEND)
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
    else:
        glDisable(GL_BLEND)
    
    # check and bind texture if available
    if mtl['map_Kd'] and mtl_name in texture_ids:
        glEnable(GL_TEXTURE_2D)
        glBindTexture(GL_TEXTURE_2D, texture_ids[mtl_name])
    else:
        glDisable(GL_TEXTURE_2D)

def display():
    # main display function
    global rotation, zoom, vertices, faces, model_center, model_scale
    global materials, face_materials
    
    # clear the screen
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)
    
    # set up the camera
    glMatrixMode(GL_PROJECTION)
    glLoadIdentity()
    gluPerspective(45, 1, 0.1, 100.0)
    
    glMatrixMode(GL_MODELVIEW)
    glLoadIdentity()
    
    # position camera
    glTranslatef(0, 0, -5.0 / zoom)
    
    # apply rotation
    glRotatef(rotation[0], 1, 0, 0)
    glRotatef(rotation[1], 0, 1, 0)
    
    # apply centering and scaling
    glTranslatef(-model_center[0], -model_center[1], -model_center[2])
    glScalef(1.0/model_scale, 1.0/model_scale, 1.0/model_scale)
    
    # enable lighting
    glEnable(GL_LIGHTING)
    glEnable(GL_LIGHT0)
    glLightfv(GL_LIGHT0, GL_POSITION, [1, 1, 1, 0])  # directional light
    glLightfv(GL_LIGHT0, GL_AMBIENT, [0.3, 0.3, 0.3, 1])
    glLightfv(GL_LIGHT0, GL_DIFFUSE, [0.7, 0.7, 0.7, 1])
    glLightfv(GL_LIGHT0, GL_SPECULAR, [1.0, 1.0, 1.0, 1])
    
    # enable color material
    glEnable(GL_COLOR_MATERIAL)
    glColorMaterial(GL_FRONT, GL_AMBIENT_AND_DIFFUSE)
    
    # draw the model with materials
    current_mtl = None
    
    for i, (face_vertices, face_texcoords, face_normals) in enumerate(faces):
        # apply material if changed
        mtl_name = face_materials[i] if i < len(face_materials) else None
        if mtl_name != current_mtl:
            current_mtl = mtl_name
            apply_material(current_mtl)
        
        # drawing using triangle mode for all polygons
        if len(face_vertices) >= 3:
            glBegin(GL_TRIANGLES)
            
            # triangulate face if it has more than 3 vertices
            for j in range(len(face_vertices) - 2):
                indices = [0, j + 1, j + 2]  # fan triangulation
                
                for idx in indices:
                    if idx < len(face_vertices):
                        # texture coordinates if available
                        if idx < len(face_texcoords) and face_texcoords[idx] >= 0 and face_texcoords[idx] < len(texcoords):
                            glTexCoord2fv(texcoords[face_texcoords[idx]])
                        
                        # normals if available
                        if idx < len(face_normals) and face_normals[idx] >= 0 and face_normals[idx] < len(normals):
                            glNormal3fv(normals[face_normals[idx]])
                        elif len(normals) == 0:  # generate simple normal if none available
                            glNormal3f(0, 0, 1)
                        
                        # vertex
                        v_idx = face_vertices[idx]
                        if v_idx < len(vertices):
                            glVertex3fv(vertices[v_idx])
            
            glEnd()
    
    # disable texturing and lighting when done
    glDisable(GL_TEXTURE_2D)
    glDisable(GL_LIGHTING)

def main():
    # check if file path is provided as command line argument
    if len(sys.argv) > 1 and os.path.isfile(sys.argv[1]) and sys.argv[1].lower().endswith('.obj'):
        file_path = sys.argv[1]
    else:
        # select file with tkinter dialog if no command line argument
        root = tk.Tk()
        root.withdraw()  # hide the root window
        file_path = filedialog.askopenfilename(
            title="Select OBJ file",
            filetypes=[("OBJ files", "*.obj"), ("All files", "*.*")]
        )
    
    # exit if no file selected
    if not file_path:
        print("No file selected. Exiting.")
        return
    
    # load the obj file
    global face_materials
    face_materials = load_obj(file_path)
    
    # initialize glfw
    if not glfw.init():
        print("Failed to initialize GLFW")
        return
    
    # create a window
    window = glfw.create_window(800, 800, "OBJ Viewer: " + os.path.basename(file_path), None, None)
    if not window:
        glfw.terminate()
        print("Failed to create GLFW window")
        return
    
    # make the window's context current
    glfw.make_context_current(window)
    
    # set callbacks
    glfw.set_mouse_button_callback(window, mouse_button_callback)
    glfw.set_cursor_pos_callback(window, cursor_position_callback)
    glfw.set_scroll_callback(window, scroll_callback)
    glfw.set_key_callback(window, key_callback)
    
    # configure opengl
    glEnable(GL_DEPTH_TEST)
    glClearColor(0.2, 0.2, 0.2, 1.0)  # dark gray background
    
    # configure for transparency
    glEnable(GL_BLEND)
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
    
    # main loop
    while not glfw.window_should_close(window):
        # render the scene
        display()
        
        # swap buffers and poll events
        glfw.swap_buffers(window)
        glfw.poll_events()
    
    # clean up
    # delete textures
    for texture_id in texture_ids.values():
        glDeleteTextures(1, [texture_id])
    
    glfw.terminate()

if __name__ == "__main__":
    main()