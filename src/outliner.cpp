/* -*- coding: utf-8-unix -*-
 *
 * File: src/outliner.cpp
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
#include "outliner.h"
#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QFile>
#include <QDir>
#include <QRegularExpression>
#include <QTextStream>
#include "chartmanager.h"
#include "s57chart.h"

Outliner::Outliner(QObject* parent)
  : Drawable(parent)
  , m_vertexBuffer(QOpenGLBuffer::VertexBuffer)
  , m_program(nullptr)
  , m_manager(ChartManager::instance())
{}

void Outliner::initializeGL() {
  if (m_program == nullptr) {
    m_program = new QOpenGLShaderProgram(this);
    struct Source {
      QOpenGLShader::ShaderTypeBit stype;
      QString fname;
    };
    const QVector<Source> sources{
      {QOpenGLShader::Vertex, ":outliner.vert"},
      {QOpenGLShader::Fragment, ":outliner.frag"},
    };
    for (const Source& s: sources) {
      if (!m_program->addShaderFromSourceFile(s.stype, s.fname)) {
        qFatal("Failed to compile %s: %s", s.fname.toUtf8().data(), m_program->log().toUtf8().data());
      }
    }
    if (!m_program->link()) {
      qFatal("Failed to link the outliner program: %s", m_program->log().toUtf8().data());
    }
    m_vertexBuffer.create();
    m_vertexBuffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);

    // locations
    m_program->bind();
    m_locations.base_color = m_program->uniformLocation("base_color");
    m_locations.m_pv = m_program->uniformLocation("m_pv");
  }
  updateBuffers();
}

void Outliner::updateCharts(const Camera* /*cam*/, const QRectF& /*viewArea*/) {
  // Update vertex buffer whenever charts or chartsets are updated/loaded
  updateBuffers();
}

void Outliner::paintGL(const Camera* cam) {
  // If outlines were loaded after initializeGL(), populate the vertex buffer on first paint
  if (m_lineStringSizes.isEmpty() && m_manager && !m_manager->outlines().isEmpty()) {
    updateBuffers();
  }

  if (m_lineStringSizes.isEmpty()) {
    return;
  }

  m_vertexBuffer.bind();
  m_program->bind();
  m_program->enableAttributeArray(0);
  m_program->setAttributeBuffer(0, GL_FLOAT, 0, 3, 0);

  auto f = QOpenGLContext::currentContext()->extraFunctions();

  // Ensure depth test uses LEQUAL so lines at or slightly above the globe surface pass cleanly
  f->glEnable(GL_DEPTH_TEST);
  f->glDepthFunc(GL_LEQUAL);
  f->glDepthMask(GL_TRUE);

  // Disable blending, stenciling, and face culling so lines are never culled
  f->glDisable(GL_BLEND);
  f->glDisable(GL_STENCIL_TEST);
  f->glDisable(GL_CULL_FACE);

  // Line width: 2.0+ for high-DPI visibility on mobile and desktop screens
  f->glLineWidth(2.5f);

  // Uniforms: transform and bright green (#00ff00) color
  m_program->setUniformValue(m_locations.m_pv, cam->projection() * cam->view());
  m_program->setUniformValue(m_locations.base_color, QColor("#00ff00"));

  int first = 0;
  for (auto s: m_lineStringSizes) {
    f->glDrawArrays(GL_LINE_STRIP, first, s);
    first += s;
  }

  m_program->disableAttributeArray(0);
  m_vertexBuffer.release();
  m_program->release();
}

void Outliner::writeVertex(GL::VertexVector& vertices, const WGS84Point& p) const {
  // Radius r = 1.004f lifts line strip slightly above the globe sphere (r = 1.000f)
  // so that Euclidean 3D chords between vertices do not dip inside the sphere
  const float r = 1.004f;
  const double lng = p.radiansLng();
  const double lat = p.radiansLat();
  vertices << static_cast<float>(r * cos(lng) * cos(lat));
  vertices << static_cast<float>(r * sin(lng) * cos(lat));
  vertices << static_cast<float>(r * sin(lat));
}

void Outliner::updateBuffers() {
  m_lineStringSizes.clear();
  if (!m_manager) {
    return;
  }

  GL::VertexVector vertices;
  const WGS84Polygon& polys = m_manager->outlines();
  const qreal D = 1.e5; // 100 km geodesic sampling step

  for (const WGS84PointVector& ps: polys) {
    const int N = ps.size();
    if (N < 2) continue;

    int lineStringSize = 0;
    for (int i = 0; i < N - 1; ++i) {
      const WGS84Bearing b = ps[i + 1] - ps[i];
      const auto K = static_cast<int>(std::ceil(b.meters() / D));
      writeVertex(vertices, ps[i]);
      for (int k = 1; k < K; ++k) {
        const double s = (static_cast<double>(k) / K);
        writeVertex(vertices, ps[i] + s * b);
      }
      lineStringSize += K;
    }
    writeVertex(vertices, ps.last());
    lineStringSize += 1;

    // Ensure chart border polygon is closed if first and last point do not match
    if (ps.first() != ps.last()) {
      const WGS84Bearing b = ps.first() - ps.last();
      const auto K = static_cast<int>(std::ceil(b.meters() / D));
      for (int k = 1; k < K; ++k) {
        const double s = (static_cast<double>(k) / K);
        writeVertex(vertices, ps.last() + s * b);
      }
      writeVertex(vertices, ps.first());
      lineStringSize += K;
    }

    m_lineStringSizes.append(lineStringSize);
  }

  if (vertices.isEmpty()) {
    return;
  }

  m_vertexBuffer.bind();
  GLsizei dataLen = vertices.size() * sizeof(GLfloat);
  m_vertexBuffer.allocate(dataLen);
  m_vertexBuffer.write(0, vertices.constData(), dataLen);
  m_vertexBuffer.release();
  qDebug() << "Outliner updated: number of points =" << vertices.size() / 3
           << "polygons =" << m_lineStringSizes.size();
}
