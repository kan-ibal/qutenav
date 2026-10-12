/* -*- coding: utf-8-unix -*-
 *
 * File: src/globe.cpp
 *
 * Copyright (C) 2021 Jukka Sirkka
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "globe.h"
#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLTexture>
#include <QDir>
#include <QRegularExpression>
#include <QTextStream>
#include <QStandardPaths>
#include <QHash>
#include <cstring>
#include "wfreader.h"
#include "types.h"
#include "platform.h"
#include "chartmanager.h"

namespace {
  // std140 layout structure for GlobeParamsBlock (size = 208 bytes, 13 x 16 bytes)
  struct alignas(16) GlobeParamsUBO {
    float m_pv[16];     // offset 0..63 (64 bytes)
    float m_p[16];      // offset 64..127 (64 bytes)
    float m_model[16];  // offset 128..191 (64 bytes)
    float lp[3];        // offset 192..203 (12 bytes)
    float ambient;      // offset 204..207 (4 bytes)
  };

  QHash<const Globe*, GLuint> g_globeUbos;
}

Globe::Globe(QObject *parent)
  : Drawable(parent)
  , m_coordBuffer(QOpenGLBuffer::VertexBuffer)
  , m_indexBuffer(QOpenGLBuffer::IndexBuffer)
  , m_program(nullptr)
  , m_globeTexture(new QOpenGLTexture(QOpenGLTexture::TargetCubeMap))
{}

Globe::~Globe() {
  if (g_globeUbos.contains(this)) {
    GLuint ubo = g_globeUbos.take(this);
    if (ubo != 0 && QOpenGLContext::currentContext()) {
      auto f = QOpenGLContext::currentContext()->functions();
      if (f) {
        f->glDeleteBuffers(1, &ubo);
      }
    }
  }
  delete m_program;
  delete m_globeTexture;
}

void Globe::initializeGL() {
  if (m_program != nullptr) return;

  m_program = new QOpenGLShaderProgram;

  struct Source {
    QOpenGLShader::ShaderTypeBit stype;
    QString fname;
  };

  const QVector<Source> sources{
    {QOpenGLShader::Vertex, ":globe.vert"},
    {QOpenGLShader::Fragment, ":globe.frag"},
  };

  for (const Source& s: sources) {
    if (!m_program->addShaderFromSourceFile(s.stype, s.fname)) {
      qFatal("Failed to compile %s: %s", s.fname.toUtf8().data(), m_program->log().toUtf8().data());
    }
  }

  if (!m_program->link()) {
    qFatal("Failed to link the globe program: %s", m_program->log().toUtf8().data());
  }

  // locations
  m_program->bind();
  m_locations.globe = m_program->uniformLocation("globe");
  m_locations.m_pv = m_program->uniformLocation("m_pv");
  m_locations.lp = m_program->uniformLocation("lp");

  // UBO Support: initialize GlobeParamsBlock uniform buffer and bind to binding point 0
  auto ef = QOpenGLContext::currentContext()->extraFunctions();
  if (ef) {
    GLuint ubo = 0;
    ef->glGenBuffers(1, &ubo);
    ef->glBindBuffer(GL_UNIFORM_BUFFER, ubo);
    ef->glBufferData(GL_UNIFORM_BUFFER, sizeof(GlobeParamsUBO), nullptr, GL_DYNAMIC_DRAW);
    ef->glBindBuffer(GL_UNIFORM_BUFFER, 0);
    g_globeUbos[this] = ubo;

    GLuint blockIndex = ef->glGetUniformBlockIndex(m_program->programId(), "GlobeParamsBlock");
    if (blockIndex != GL_INVALID_INDEX) {
      ef->glUniformBlockBinding(m_program->programId(), blockIndex, 0);
    }
  }

  QStringList objectFiles;
  QDir globeDir;
  QStringList locs;

  for (const QString& loc: QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
    locs << QString("%1/%2/globe").arg(loc).arg(Platform::base_app_name());
  }

  for (const QString& loc: locs) {
    globeDir = QDir(loc);
    objectFiles = globeDir.entryList(QStringList() << "sphere.obj",
                                     QDir::Files | QDir::Readable);
    if (!objectFiles.isEmpty()) break;
  }

  if (objectFiles.isEmpty()) {
    throw ChartFileError(QString("Could not find a valid globe directory in %1").arg(locs.join(", ")));
  }

  WFReader p;

  auto path = globeDir.absoluteFilePath(objectFiles.first());
  p.parse(path);

  // copy vertices/indices to buffers
  m_coordBuffer.create();
  m_coordBuffer.bind();
  m_coordBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
  GLsizei dataLen = p.vertices().size() * sizeof(GLfloat);
  m_coordBuffer.allocate(p.vertices().constData(), dataLen);

  m_indexBuffer.create();
  m_indexBuffer.bind();
  m_indexBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
  GLsizei elemLen = p.triangles().size() * sizeof(GLuint);
  m_indexBuffer.allocate(p.triangles().constData(), elemLen);

  m_indexCount = p.triangles().size();

  m_program->setAttributeBuffer(0, GL_FLOAT, 0, 3, 0);
  m_program->enableAttributeArray(0);

  // cubemap texture loading:
  // Support both single composite image (texture.png/jpg) and 6 individual face images (*.jpg/png)
  QStringList compositeCandidates = globeDir.entryList(
    QStringList() << "texture.png" << "texture.jpg" << "texture.jpeg"
                  << "globe.png" << "globe.jpg" << "cubemap.png" << "cubemap.jpg",
    QDir::Files | QDir::Readable);

  QStringList multiCandidates = globeDir.entryList(
    QStringList() << "*.jpg" << "*.jpeg" << "*.png" << "*.JPG" << "*.PNG" << "*.JPEG",
    QDir::Files | QDir::Readable);

  if (!compositeCandidates.isEmpty() && multiCandidates.size() < 6) {
    loadImage(globeDir);
  } else if (!multiCandidates.isEmpty()) {
    loadImages(globeDir);
  } else if (!compositeCandidates.isEmpty()) {
    loadImage(globeDir);
  } else {
    qWarning() << "No globe texture images found in" << globeDir.absolutePath();
  }
}

void Globe::updateCharts(const Camera* /*cam*/, const QRectF& /*viewArea*/) {
  // noop - in globe view, chart objects are not loaded into 2D viewport;
  // chart boundaries on the globe are drawn by Outliner.
}

void Globe::paintGL(const Camera *cam) {
  auto f = QOpenGLContext::currentContext()->functions();
  auto ef = QOpenGLContext::currentContext()->extraFunctions();
  f->glDisable(GL_BLEND);
  f->glEnable(GL_DEPTH_TEST);
  f->glEnable(GL_STENCIL_TEST);
  f->glEnable(GL_CULL_FACE);
  f->glFrontFace(GL_CW);
  f->glCullFace(GL_BACK);
  f->glStencilFunc(GL_EQUAL, 0, 0xff);
  f->glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
  f->glStencilMask(0xff);
  m_program->bind();

  m_program->enableAttributeArray(0);
  m_coordBuffer.bind();
  m_indexBuffer.bind();

  m_program->setAttributeBuffer(0, GL_FLOAT, 0, 3, 0);

  if (m_globeTexture && m_globeTexture->isCreated()) {
    m_globeTexture->bind(0);
  }

  const QVector3D eye = cam->view().inverted().column(3).toVector3D();
  const QVector3D lightDir = (eye.lengthSquared() > 0.0001f) ? eye.normalized() : QVector3D(0.0f, 0.0f, 1.0f);
  const QMatrix4x4 pvMatrix = cam->projection() * cam->view();
  const QMatrix4x4 pMatrix = cam->projection();
  QMatrix4x4 modelMatrix;
  modelMatrix.setToIdentity();

  // UBO Support: upload to GlobeParamsBlock (std140 layout, 208 bytes) and bind to binding point 0
  GLuint ubo = g_globeUbos.value(this, 0);
  if (ubo != 0 && ef) {
    GlobeParamsUBO uboData;
    memcpy(uboData.m_pv, pvMatrix.constData(), 16 * sizeof(float));
    memcpy(uboData.m_p, pMatrix.constData(), 16 * sizeof(float));
    memcpy(uboData.m_model, modelMatrix.constData(), 16 * sizeof(float));
    uboData.lp[0] = lightDir.x();
    uboData.lp[1] = lightDir.y();
    uboData.lp[2] = lightDir.z();
    uboData.ambient = 0.35f;

    ef->glBindBuffer(GL_UNIFORM_BUFFER, ubo);
    ef->glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(GlobeParamsUBO), &uboData);
    ef->glBindBuffer(GL_UNIFORM_BUFFER, 0);

    ef->glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);
  }

  // Fallback for default uniform block (loose uniforms)
  if (m_locations.globe != -1) {
    m_program->setUniformValue(m_locations.globe, 0);
  }
  if (m_locations.lp != -1) {
    m_program->setUniformValue(m_locations.lp, lightDir);
  }
  if (m_locations.m_pv != -1) {
    m_program->setUniformValue(m_locations.m_pv, pvMatrix);
  }

  f->glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);

  // Restore OpenGL state so S57 charts and overlays render properly without interference
  // 1. Disable stencil test and reset mask/func so S57 chart features are not culled by the globe mask
  f->glDisable(GL_STENCIL_TEST);
  f->glStencilMask(0xff);
  f->glStencilFunc(GL_ALWAYS, 0, 0xff);
  f->glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

  // 2. Disable face culling and restore standard CCW front face (S57 polygons/text use CCW winding)
  f->glDisable(GL_CULL_FACE);
  f->glFrontFace(GL_CCW);
  f->glCullFace(GL_BACK);

  // 3. Re-enable alpha blending for translucent depth areas, vector/raster symbols, and text soundings
  f->glEnable(GL_BLEND);
  f->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // 4. Release cubemap texture so it does not conflict with 2D chart textures on unit 0
  if (m_globeTexture && m_globeTexture->isCreated()) {
    m_globeTexture->release();
  }

  // 5. Ensure depth test function is GL_LEQUAL and depth mask is true so chart borders are visible
  f->glDepthFunc(GL_LEQUAL);
  f->glDepthMask(GL_TRUE);

  // 6. Clean up attribute arrays and buffers
  m_program->disableAttributeArray(0);
  m_coordBuffer.release();
  m_indexBuffer.release();
  m_program->release();
}

void Globe::loadImages(const QDir& imageDir) {
  // Support .jpg, .jpeg, and .png cubemap face textures
  auto imageFiles = imageDir.entryList(
    QStringList() << "*.jpg" << "*.jpeg" << "*.png" << "*.JPG" << "*.PNG" << "*.JPEG",
    QDir::Files | QDir::Readable, QDir::Name);
  qDebug() << "Found globe face files:" << imageFiles;

  // If fewer than 6 face files exist, attempt loading a composite image instead
  if (imageFiles.size() < 6) {
    loadImage(imageDir);
    return;
  }

  // Map files to cubemap faces by inspecting filenames (handles posx/negx/etc. naming)
  QMap<QOpenGLTexture::CubeMapFace, QImage> faceMap;

  const QVector<QOpenGLTexture::CubeMapFace> fallbackFaces {
    QOpenGLTexture::CubeMapNegativeX,
    QOpenGLTexture::CubeMapNegativeY,
    QOpenGLTexture::CubeMapNegativeZ,
    QOpenGLTexture::CubeMapPositiveX,
    QOpenGLTexture::CubeMapPositiveY,
    QOpenGLTexture::CubeMapPositiveZ,
  };

  for (const QString& file: imageFiles) {
    QString lower = file.toLower();
    QImage img = QImage(imageDir.absoluteFilePath(file)).convertToFormat(QImage::Format_RGBA8888);
    if (img.isNull()) continue;

    if (lower.contains("negx") || lower.contains("nx") || lower.contains("left")) {
      faceMap[QOpenGLTexture::CubeMapNegativeX] = img;
    } else if (lower.contains("posx") || lower.contains("px") || lower.contains("right")) {
      faceMap[QOpenGLTexture::CubeMapPositiveX] = img;
    } else if (lower.contains("negy") || lower.contains("ny") || lower.contains("bottom") || lower.contains("down")) {
      faceMap[QOpenGLTexture::CubeMapNegativeY] = img;
    } else if (lower.contains("posy") || lower.contains("py") || lower.contains("top") || lower.contains("up")) {
      faceMap[QOpenGLTexture::CubeMapPositiveY] = img;
    } else if (lower.contains("negz") || lower.contains("nz") || lower.contains("back")) {
      faceMap[QOpenGLTexture::CubeMapNegativeZ] = img;
    } else if (lower.contains("posz") || lower.contains("pz") || lower.contains("front")) {
      faceMap[QOpenGLTexture::CubeMapPositiveZ] = img;
    }
  }

  // If filename-based matching did not resolve all 6 faces, use alphabetical order
  if (faceMap.size() < 6) {
    faceMap.clear();
    for (int i = 0; i < qMin(imageFiles.size(), fallbackFaces.size()); ++i) {
      QImage img = QImage(imageDir.absoluteFilePath(imageFiles[i])).convertToFormat(QImage::Format_RGBA8888);
      if (!img.isNull()) {
        faceMap[fallbackFaces[i]] = img;
      }
    }
  }

  if (faceMap.isEmpty()) {
    qWarning() << "Failed to load any valid globe images from" << imageDir.absolutePath();
    return;
  }

  QImage firstImage = faceMap.first();
  const int w = firstImage.width();
  const int h = firstImage.height();

  if (m_globeTexture->isCreated()) {
    m_globeTexture->destroy();
  }
  m_globeTexture->create();
  m_globeTexture->bind(0);

  // Set 2D face dimensions for TargetCubeMap (depth is 1, not image.depth() which is 32 bits!)
  m_globeTexture->setSize(w, h);
  m_globeTexture->setFormat(QOpenGLTexture::RGBA8_UNorm);
  m_globeTexture->allocateStorage();

  const QVector<QOpenGLTexture::CubeMapFace> allFaces {
    QOpenGLTexture::CubeMapNegativeX,
    QOpenGLTexture::CubeMapPositiveX,
    QOpenGLTexture::CubeMapNegativeY,
    QOpenGLTexture::CubeMapPositiveY,
    QOpenGLTexture::CubeMapNegativeZ,
    QOpenGLTexture::CubeMapPositiveZ,
  };

  for (auto face : allFaces) {
    QImage img = faceMap.value(face);
    if (img.isNull() || img.width() != w || img.height() != h) {
      img = firstImage;
    }
    m_globeTexture->setData(0, 0, face,
                            QOpenGLTexture::RGBA, QOpenGLTexture::UInt8,
                            img.constBits(), nullptr);
  }

  m_globeTexture->setWrapMode(QOpenGLTexture::ClampToEdge);
  // Generating mipmaps ensures texture is complete and visible when sampled with LinearMipMapLinear
  m_globeTexture->generateMipMaps();
  m_globeTexture->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
  m_globeTexture->setMagnificationFilter(QOpenGLTexture::Linear);
}

void Globe::loadImage(const QDir& imageDir) {
  // Support composite cross images (texture.png, texture.jpg, globe.png, etc.)
  auto imageFiles = imageDir.entryList(
    QStringList() << "texture.png" << "texture.jpg" << "texture.jpeg"
                  << "globe.png" << "globe.jpg" << "cubemap.png" << "cubemap.jpg",
    QDir::Files | QDir::Readable, QDir::Name);
  qDebug() << "Composite image candidates:" << imageFiles;

  if (imageFiles.isEmpty()) {
    qWarning() << "No composite texture file found in" << imageDir.absolutePath();
    return;
  }

  auto image = QImage(imageDir.absoluteFilePath(imageFiles.first()))
      .convertToFormat(QImage::Format_RGBA8888);

  if (image.isNull() || image.width() < 4 || image.height() < 3) {
    qWarning() << "Invalid composite texture image in" << imageFiles.first();
    return;
  }

  const int w = image.width() / 4;
  const int h = image.height() / 3;

  if (m_globeTexture->isCreated()) {
    m_globeTexture->destroy();
  }
  m_globeTexture->create();
  m_globeTexture->bind(0);

  // Set 2D face dimensions for TargetCubeMap (depth is 1, not image.depth() which is 32 bits!)
  m_globeTexture->setSize(w, h);
  m_globeTexture->setFormat(QOpenGLTexture::RGBA8_UNorm);
  m_globeTexture->allocateStorage();

  struct CubeData {
    QOpenGLTexture::CubeMapFace face;
    int xOffset;
    int yOffset;
  };

  QVector<CubeData> faces {
    {QOpenGLTexture::CubeMapNegativeX, 0, h},     // left
    {QOpenGLTexture::CubeMapPositiveX, 2 * w, h}, // right
    {QOpenGLTexture::CubeMapNegativeY, w, 2 * h}, // bottom
    {QOpenGLTexture::CubeMapPositiveY, w, 0},     // top
    {QOpenGLTexture::CubeMapNegativeZ, 3 * w, h}, // back
    {QOpenGLTexture::CubeMapPositiveZ, w, h},     // front
  };

  for (const CubeData& face: faces) {
    auto subimage = image.copy(face.xOffset, face.yOffset, w, h);
    m_globeTexture->setData(0, 0, face.face,
                            QOpenGLTexture::RGBA, QOpenGLTexture::UInt8,
                            subimage.constBits(), nullptr);
  }

  m_globeTexture->setWrapMode(QOpenGLTexture::ClampToEdge);
  // Generating mipmaps ensures texture is complete and visible when sampled with LinearMipMapLinear
  m_globeTexture->generateMipMaps();
  m_globeTexture->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
  m_globeTexture->setMagnificationFilter(QOpenGLTexture::Linear);
}

