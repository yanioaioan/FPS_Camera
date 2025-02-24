#include <QMouseEvent>
#include <QGuiApplication>

#include "NGLScene.h"
#include <ngl/Camera.h>
#include <ngl/Light.h>
#include <ngl/Material.h>
#include <ngl/NGLInit.h>
#include <ngl/VAOPrimitives.h>
#include <ngl/ShaderLib.h>
#include <memory>

#include <Magick++.h>
#include <Magick++/Blob.h>


//----------------------------------------------------------------------------------------------------------------------
/// @brief the increment for x/y translation with mouse movement
//----------------------------------------------------------------------------------------------------------------------
const static float INCREMENT=0.01f;
//----------------------------------------------------------------------------------------------------------------------
/// @brief the increment for the wheel zoom
//----------------------------------------------------------------------------------------------------------------------
const static float ZOOM=0.1f;

struct data
  {
    ngl::Vec2 po;
  };


static std::unique_ptr<GLubyte> pixels = NULL;
static const GLenum FORMAT = GL_RGBA;
static const GLuint FORMAT_NBYTES = 4;
//static  unsigned int HEIGHT = 500;
//static  unsigned int WIDTH = 500;
static unsigned int nscreenshots = 0;


static void create_ppm(char *prefix, int frame_id, unsigned int width, unsigned int height,
        unsigned int color_max, unsigned int pixel_nbytes, const std::unique_ptr<GLubyte> &pixels) {
    size_t i, j, k, cur;
    enum Constants { max_filename = 256 };
    char filename[max_filename];
    snprintf(filename, max_filename, "%s%d.ppm", prefix, frame_id);
    FILE *f = fopen(filename, "w");
    fprintf(f, "P3\n%d %d\n%d\n", width, height, 255);
    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++) {
            cur = pixel_nbytes * ((height - i - 1) * width + j);
            fprintf(f, "%3d %3d %3d ", pixels.get()[cur], pixels.get()[cur + 1], pixels.get()[cur + 2]);
        }
        fprintf(f, "\n");
    }
    fclose(f);
}

NGLScene::NGLScene()
{
  // re-size the widget to that of the parent (in that case the GLFrame passed in on construction)
  m_rotate=false;
  // mouse rotation values set to 0
  m_spinXFace=0.0f;
  m_spinYFace=0.0f;
  setTitle("Qt5 Simple NGL Demo");

  cameraSpeed =0.05;

  memset(keys, false, sizeof(keys)/sizeof(bool));

  //needed for properly handling saving screenshots while resizing (see resizeGL)
  m_width=0;
  m_height=0;

}


NGLScene::~NGLScene()
{
  m_vao->removeVOA();
  std::cout<<"Shutting down NGL, removing VAO's and Shaders\n";
}




static bool SphereToPlane(const ngl::Vec3& centerObjPos, const ngl::Vec3& planePoint, const ngl::Vec3& planePointNormal)
    {
        //Calculate a vector from the point on the plane to the center of the sphere
        ngl::Vec3 vecTemp(centerObjPos - planePoint);

        //Calculate the distance: dot product of the new vector with the plane's normal
        float fDist= vecTemp.dot(planePointNormal);

        float radius=1;
        if(fDist > radius)
        {
            //The sphere is not touching the plane
            return false;
        }

        //Else, the sphere is colliding with the plane
        return true;
    }


#define MAX(x,y) (((x) < (y)) ? (y) : (x))

static float j=0;
ngl::Vec3 NGLScene::calculateCoulombFriction(ngl::Vec3 & velocity)
{
  /*
    jt is the magnitude of the friction impulse (pre-cone limit)
    u is the coefficient of friction [0,1]
    t in the tangent vector in the direction of sliding
    v is the the go stone velocity at the contact point
    r is the contact point minus the center of the go stone
    I is the inertia tensor of the go stone
    m is the mass of the go stone
  */
//    ngl::Vec3 u(1,0,0);
//    float m=1;

//    jt= -velocity

//    float d = velocity.dot (normal);
//    float mag= - ( 1 + e ) * d;
//    float j = MAX( mag, 0.0 );

    j+=0.1;
    velocity += -j* ngl::Vec3(0,1,0);

    velocity.m_y= MAX(velocity.m_y,0.0);

    //Stabilized, reset so as to let player press Jump-space again
    if (velocity.m_y==0)
    {
        keys[4]=false;
        velocity.set(0,-10,0);
        j=0;
    }

    return velocity;
}



float dt = 0.1;

ngl::Vec3 velocity(0,0,0);
ngl::Vec3 position = 0;
ngl::Vec3 force(0,-9.8,0);
float mass = 1;
ngl::Vec3 groundNormal (0,1,0);

float e=0.8; //when e=0 ->  collision is perfectly inelastic, when e=1 ->  collision is perfectly elastic;

ngl::Vec3 NGLScene::calculateCollisionResponse(const ngl::Vec3 & normal)
{
    float d = velocity.dot (normal);
    float mag= - ( 1 + e ) * d;
    float j = MAX( mag, 0.0 );
    velocity += j* normal;
    return velocity;
}

void NGLScene::resizeGL(QResizeEvent *_event)
{
  // now set the camera size values as the screen size has changed
  m_cam.setShape(45.0f,(float)width()/height(),0.05f,350.0f);

  //first time it runs
  if (pixels==nullptr && m_width==0 && m_height==0)
  {
      m_width=_event->size().width()*devicePixelRatio();
      m_height=_event->size().height()*devicePixelRatio();
      pixels.reset(new GLubyte[FORMAT_NBYTES * m_width * m_height]);
  }
  else //delete and allocate properly sized memory to hold the new framebuffer
  {
      m_width=_event->size().width()*devicePixelRatio();
      m_height=_event->size().height()*devicePixelRatio();
      pixels.reset(new GLubyte[FORMAT_NBYTES * m_width * m_height]);

  }


}

void NGLScene::resizeGL(int _w , int _h)
{
  m_cam.setShape(45.0f,(float)_w/_h,0.05f,350.0f);

  // now set the camera size values as the screen size has changed
  if (pixels==nullptr && m_width==0 && m_height==0)
  {
      m_width=_w*devicePixelRatio();
      m_height=_h*devicePixelRatio();
      pixels.reset(new GLubyte[FORMAT_NBYTES * m_width * m_height]);
  }
  else //delete and allocate properly sized memory to hold the new framebuffer
  {
      m_width=_w*devicePixelRatio();
      m_height=_h*devicePixelRatio();
      pixels.reset(new GLubyte[FORMAT_NBYTES * m_width * m_height]);

  }


}

void NGLScene::initializeGL()
{
  // we must call that first before any other GL commands to load and link the
  // gl commands from the lib, if that is not done program will crash
  ngl::NGLInit::instance();
  glClearColor(0.4f, 0.4f, 0.4f, 1.0f);			   // Grey Background
  // enable depth testing for drawing
  glEnable(GL_DEPTH_TEST);
  // enable multisampling for smoother drawing
  glEnable(GL_MULTISAMPLE);
   // now to load the shader and set the values
  // grab an instance of shader manager
  ngl::ShaderLib *shader=ngl::ShaderLib::instance();
  // we are creating a shader called Phong
  shader->createShaderProgram("Phong");
  // now we are going to create empty shaders for Frag and Vert
  shader->attachShader("PhongVertex",ngl::ShaderType::VERTEX);
  shader->attachShader("PhongFragment",ngl::ShaderType::FRAGMENT);
  // attach the source
  shader->loadShaderSource("PhongVertex","shaders/PhongVertex.glsl");
  shader->loadShaderSource("PhongFragment","shaders/PhongFragment.glsl");
  // compile the shaders
  shader->compileShader("PhongVertex");
  shader->compileShader("PhongFragment");
  // add them to the program
  shader->attachShaderToProgram("Phong","PhongVertex");
  shader->attachShaderToProgram("Phong","PhongFragment");
  // now bind the shader attributes for most NGL primitives we use the following
  // layout attribute 0 is the vertex data (x,y,z)
  shader->bindAttribute("Phong",0,"inVert");
  // attribute 1 is the UV data u,v (if present)
  shader->bindAttribute("Phong",1,"inUV");
  // attribute 2 are the normals x,y,z
  shader->bindAttribute("Phong",2,"inNormal");

  // now we have associated that data we can link the shader
  shader->linkProgramObject("Phong");
  // and make it active ready to load values
  (*shader)["Phong"]->use();
  // the shader will use the currently active material and light0 so set them
  ngl::Material m(ngl::STDMAT::COPPER);
  // load our material values to the shader into the structure material (see Vertex shader)
  m.loadToShader("material");
  // Now we will create a basic Camera from the graphics library
  // This is a static camera so it only needs to be set once
  // First create Values for the camera position
  ngl::Vec3 from(0,1,1);
  ngl::Vec3 to(0,0,0);
  ngl::Vec3 up(0,1,0);
  // now load to our new camera
  m_cam.set(from,to,up);
  // set the shape using FOV 45 Aspect Ratio based on Width and Height
  // The final two are near and far clipping planes of 0.5 and 10
  m_cam.setShape(45.0f,(float)720.0/576.0f,0.05f,350.0f);
  shader->setUniform("viewerPos",m_cam.getEye().toVec3());
  // now create our light that is done after the camera so we can pass the
  // transpose of the projection matrix to the light to do correct eye space
  // transformations
  ngl::Mat4 iv=m_cam.getViewMatrix();
  iv.transpose();
  ngl::Light light(ngl::Vec3(-2,5,2),ngl::Colour(1,1,1,1),ngl::Colour(1,1,1,1),ngl::LightModes::POINTLIGHT );
  light.setTransform(iv);
  // load these values to the shader as well
  light.loadToShader("light");
  // as re-size is not explicitly called we need to do that.
  // set the viewport for openGL we need to take into account retina display


  //create a line VAO
  buildVAO();  

  startTimer(10);

  currentCameraPos.set(0,5,15);
  prevCameraPos=currentCameraPos;
  currentCameraUp.set(0,1,0);
  currentCameraFront.set(0,0,-1);


  //glReadPixels will read from back buffer
  glReadBuffer(GL_BACK);

}

static float rot=0.0f;
void NGLScene::loadMatricesToShader()
{
  updateCameraPos();  

  ngl::ShaderLib *shader=ngl::ShaderLib::instance();

  ngl::Mat4 MV;
  ngl::Mat4 MVP;
  ngl::Mat3 normalMatrix;
  ngl::Mat4 M;


  //camera stuff
  radius=2;
  toradians=M_PI/180.0f;
  _x = radius * sin( rot*toradians);
  _z = radius * cos( rot*toradians);
//  currentCameraorigin.set(0,0,0);
//  currentCameraUp.set(0,1,0);
//  currentCameraFront.set(0,0,-1);
//  currentCameraPos.set(ngl::Vec3( 2*_x, 2, 2*_z ));
//  viewMatrix=ngl::lookAt(currentCameraPos, currentCameraorigin, currentCameraUp );


  //update front vector, calculated from m_mouseGlobalTX (based on rotX, rotY mouse spining )
  currentCameraFront=m_mouseGlobalTX.getForwardVector();

  //get current camera position matrix
  M=m_transform.getMatrix();//*m_mouseGlobalTX;

  //calculate viewMatrix based on currentCameraPos, currentCameraPos + currentCameraFront, and currentCameraUp
  viewMatrix=ngl::lookAt(currentCameraPos, currentCameraPos + currentCameraFront, currentCameraUp);

  ngl::Mat4 projectionMatrix=ngl::perspective(45.0f, 1024/768, 0.5f, 200.0f);

  MV=  M*viewMatrix;//m_cam.getViewMatrix();
  MVP= MV*projectionMatrix;//m_cam.getVPMatrix();
  normalMatrix=MV;
  normalMatrix.inverse();
  shader->setShaderParamFromMat4("MV",MV);
  shader->setShaderParamFromMat4("MVP",MVP);
  shader->setShaderParamFromMat3("normalMatrix",normalMatrix);
  shader->setShaderParamFromMat4("M",M);
}


void NGLScene::buildVAO()
{

    std::vector <data> points;
    data p;
    p.po.m_x=-0.5;
    p.po.m_y=-0.5;
    points.push_back(p);

    p.po.m_x=0.5;
    p.po.m_y=1.0;
    points.push_back(p);


      m_vao = ngl::VertexArrayObject::createVOA(GL_LINES);
      m_vao->bind();
      m_vao->setData(sizeof(points),points[0].po.m_x,GL_STATIC_DRAW);

      m_vao->setVertexAttributePointer(0,2,GL_FLOAT,0,0);

      m_vao->setNumIndices(sizeof(points)/sizeof(ngl::Vec2));
      m_vao->unbind();

}

void NGLScene::paintGL()
{
  glViewport(0,0,m_width,m_height);
  // clear the screen and depth buffer
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // grab an instance of the shader manager
  ngl::ShaderLib *shader=ngl::ShaderLib::instance();
  (*shader)["Phong"]->use();

  // Rotation based on the mouse position for our global transform
  ngl::Mat4 rotX;
  ngl::Mat4 rotY;
  // create the rotation matrices
  if (m_spinXFace> 89.0f)
      m_spinXFace= 89.0f;
  if (m_spinXFace< -89.0f)
      m_spinXFace= -89.0f;


//  std::cout<<"m_spinXFace="<<m_spinXFace<<std::endl;
  rotX.rotateX(m_spinXFace);
  rotY.rotateY(m_spinYFace);
  // multiply the rotations
  m_mouseGlobalTX=rotY*rotX;
  // add the translations
  m_mouseGlobalTX.m_m[3][0] = m_modelPos.m_x;
  m_mouseGlobalTX.m_m[3][1] = m_modelPos.m_y;
  m_mouseGlobalTX.m_m[3][2] = m_modelPos.m_z;

   // get the VBO instance and draw the built in teapot
  ngl::VAOPrimitives *prim=ngl::VAOPrimitives::instance();
  // draw



      m_transform.reset();
      m_transform.setPosition(-2,-3,0);
      loadMatricesToShader();
      prim->draw("teapot");
      m_transform.setPosition(2,3,0);
      loadMatricesToShader();
      prim->draw("teapot");
      m_transform.setPosition(-2,4,-5);
      loadMatricesToShader();
      prim->draw("teapot");
      m_transform.setPosition(2,-5,5);
      loadMatricesToShader();
      prim->draw("teapot");

    m_transform.reset();
    loadMatricesToShader();
    m_vao->bind();
    m_vao->draw();
    m_vao->unbind();


}

//----------------------------------------------------------------------------------------------------------------------
void NGLScene::mouseMoveEvent (QMouseEvent * _event)
{
  // note the method buttons() is the button state when event was called
  // that is different from button() which is used to check which button was
  // pressed when the mousePress/Release event is generated
  if(m_rotate && _event->buttons() == Qt::LeftButton)
  {
    int diffx=_event->x()-m_origX;
    int diffy=_event->y()-m_origY;
    m_spinXFace += (float) 0.2f * diffy;
    m_spinYFace += (float) 0.2f * diffx;
    m_origX = _event->x();
    m_origY = _event->y();
    update();

  }
        // right mouse translate code
  else if(m_translate && _event->buttons() == Qt::RightButton)
  {
    int diffX = (int)(_event->x() - m_origXPos);
    int diffY = (int)(_event->y() - m_origYPos);
    m_origXPos=_event->x();
    m_origYPos=_event->y();
    m_modelPos.m_x += INCREMENT * diffX;
    m_modelPos.m_y -= INCREMENT * diffY;
    update();

   }
}


//----------------------------------------------------------------------------------------------------------------------
void NGLScene::mousePressEvent ( QMouseEvent * _event)
{
  // that method is called when the mouse button is pressed in this case we
  // store the value where the maouse was clicked (x,y) and set the Rotate flag to true
  if(_event->button() == Qt::LeftButton)
  {
    m_origX = _event->x();
    m_origY = _event->y();
    m_rotate =true;
  }
  // right mouse translate mode
  else if(_event->button() == Qt::RightButton)
  {
    m_origXPos = _event->x();
    m_origYPos = _event->y();
    m_translate=true;
  }


}

//----------------------------------------------------------------------------------------------------------------------
void NGLScene::mouseReleaseEvent ( QMouseEvent * _event )
{
  // that event is called when the mouse button is released
  // we then set Rotate to false
  if (_event->button() == Qt::LeftButton)
  {
    m_rotate=false;
  }
        // right mouse translate mode
  if (_event->button() == Qt::RightButton)
  {
    m_translate=false;
  }
}

//----------------------------------------------------------------------------------------------------------------------
void NGLScene::wheelEvent(QWheelEvent *_event)
{

	// check the diff of the wheel position (0 means no change)
	if(_event->delta() > 0)
	{
		m_modelPos.m_z+=ZOOM;
	}
	else if(_event->delta() <0 )
	{
		m_modelPos.m_z-=ZOOM;
	}
	update();
}
//----------------------------------------------------------------------------------------------------------------------

void NGLScene::keyPressEvent(QKeyEvent *_event)
{
  // that method is called every time the main window recives a key event.
  // we then switch on the key value and set the camera in the GLWindow
  switch (_event->key())
  {
  // escape key to quit
  case Qt::Key_Escape : QGuiApplication::exit(EXIT_SUCCESS); break;
  // turn on wirframe rendering
//  case Qt::Key_W : glPolygonMode(GL_FRONT_AND_BACK,GL_LINE); break;
  // turn off wire frame
//  case Qt::Key_S : glPolygonMode(GL_FRONT_AND_BACK,GL_FILL); break;
  // show full screen
  case Qt::Key_F : showFullScreen(); break;
  // show windowed
  case Qt::Key_N : showNormal(); break;


  case Qt::Key_W :
  {
      keys[0] = true;
      std::cout<<"Up Pressed"<<std::endl;
      break;
  }
  case Qt::Key_S:
  {
      keys[1] = true;
      std::cout<<"Down Pressed"<<std::endl;
      break;
  }
  case Qt::Key_A :
  {
      keys[2] = true;
      std::cout<<"Left Pressed"<<std::endl;
      break;
  }
  case Qt::Key_D :
  {
      keys[3] = true;
      std::cout<<"Right Pressed"<<std::endl;
      break;
  }
  case Qt::Key_Space :
  {
      keys[4] = true;
      std::cout<<"Space Pressed"<<std::endl;
      break;
  }
  case Qt::Key_P :
  {
      glReadPixels(0, 0, m_width, m_height, FORMAT, GL_UNSIGNED_BYTE, pixels.get());

      //create_ppm("tmp", nscreenshots, m_width, m_height, 255, FORMAT_NBYTES, pixels);



//      Magick::Blob b( pixels.get(), FORMAT_NBYTES * m_width * m_height );
//      Magick::Image saveimage(b);
//      saveimage.write("subimageGcrop.png");

      // now create an image data block
      Magick::Image output(m_width,m_height,"RGBA",Magick::CharPixel,pixels.get());
      // set the output image depth to 16 bit
      output.depth(16);
      // write the file
      output.write("Test.png");


      nscreenshots++;
      std::cout<<"Save Image"<<std::endl;
      break;
  }





  }
  // finally update the GLWindow and re-draw
  //if (isExposed())
    update();
}

void NGLScene::keyReleaseEvent(QKeyEvent *_event)
{
    switch (_event->key())
    {
        case Qt::Key_W :
        {
            keys[0] = false;
            std::cout<<"Up Released"<<std::endl;
            break;
        }
        case Qt::Key_S:
        {
            keys[1] = false;
            std::cout<<"Down Released"<<std::endl;
            break;
        }
        case Qt::Key_A :
        {
            keys[2] = false;
            std::cout<<"Left Released"<<std::endl;
            break;
        }
        case Qt::Key_D :
        {
            keys[3] = false;
            std::cout<<"Right Released"<<std::endl;
            break;
        }
        case Qt::Key_Space :
        {
            keys[4] = false;
            velocity.m_y = 0;
            std::cout<<"Space Pressed"<<std::endl;
            break;
        }



    }
}

static float friction=0.0f;
float a=1.1;
float b=2.3;
void NGLScene::timerEvent(QTimerEvent * _event)
{
//    rot+=0.15;

//    //Jump implementation - rbd collision with artificial friction
//    if (keys[4]==true)
//    {
//        //u=a* ,a=f/mt
//        //friction+=0.1;
//        velocity +=  ( force / mass ) * dt;



//        ngl::Vec3 planeCenter(0,0.01,0);
//        ngl::Vec3 planeNormal(0,1,0);

//        //correct velocity - apply response impulse if hits the ground
//        bool collidedwithPlane = SphereToPlane (currentCameraPos, planeCenter, planeNormal);

//        if (collidedwithPlane)
//        {
//            velocity = calculateCollisionResponse(planeNormal);

//            velocity = calculateCoulombFriction(velocity);
//        }

//        currentCameraPos += velocity * dt;
//    }




    //Jump implementation - rbd collision with artificial friction
    if (keys[4]==true)
    {
        if (currentCameraPos.m_y == 0) // >=  lets jump on the top of jump
        {
            velocity.m_y = 20;
        }
        keys[4]=false;
    }

    //u=a*t
    velocity +=  (force/mass) *dt;
    //x=u*t..euler integration..
    currentCameraPos += velocity * dt;


//    //save current pos
//    ngl::Vec3 tempPos=currentCameraPos;
//    //update to new pos using prev pos
//    currentCameraPos += currentCameraPos-prevCameraPos+(force/mass) *dt*dt;
//    //xi+1 = xi + (xi - xi-1) + a * dt * dt , verlet integration

//    //set previous camera position (to be used in Verlet Integration)
//    prevCameraPos=tempPos;




    //force camera position to stay above y=0
    if(currentCameraPos.m_y<0)
    {
        currentCameraPos.m_y=0;
        velocity.m_y=0;
    }

    std::cout<<"velocity="<<velocity.m_y<<std::endl;




    update();
}




void NGLScene::updateCameraPos()
{
    //    if(sizeof(keys)!=0)
    {
        if (keys[0]==true) currentCameraPos += cameraSpeed * currentCameraFront;
        if (keys[1]==true) currentCameraPos -= cameraSpeed * currentCameraFront;
        if (keys[2]==true )// move left
        {
            //get right vector ---> front X up
            currentRight.set(currentCameraFront.cross(currentCameraUp) );
            //normalize it
            currentRight.normalize();// normalize cause we are interested only in the direction of the right vector
            //set new camera position
            newCamPos.set( currentRight * cameraSpeed );//move it to a new left position
            currentCameraPos -=newCamPos;
        }
        if (keys[3]==true)// move right
        {
            //get right vector ---> front X up
            currentRight.set(currentCameraFront.cross(currentCameraUp) );// ---> front X up
            //normalize it
            currentRight.normalize();// normalize cause we are interested only in the direction of the right vector
            //set new camera position
            newCamPos.set( currentRight * cameraSpeed );//move it to a new right position
            currentCameraPos +=newCamPos;
        }
    }

    //make Camera stay at a certain y level (if wanted)
//    currentCameraPos.m_y=0;
//    std::cout<<"currentCameraPos.m_y="<<currentCameraPos.m_y<<std::endl;



}
