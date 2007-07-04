/*
 *  Copyright (c) 1999 Matthias Elter <me@kde.org>
 *  Copyright (c) 2002 Patrick Julien <freak@codepimps.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */
#include <string.h>

#include <QPoint>
#include <QLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QListWidget>
#include <QList>
#include <QWidget>

#include <kactioncollection.h>
#include <kaction.h>
#include <klocale.h>
#include <kmessagebox.h>

#include "kis_layer.h"
#include "kis_cursor.h"
#include "kis_image.h"
#include "kis_paint_device.h"
#include "kis_tool_colorpicker.h"
#include "kis_tool_colorpicker.moc"
#include "KoPointerEvent.h"
#include "KoCanvasBase.h"
#include "kis_iterators_pixel.h"
#include "KoColor.h"
#include "kis_resourceserver.h"
#include "KoColorSet.h"

namespace {
    // The location of the sample all visible layers in the combobox
    const int SAMPLE_MERGED = 0;
}

KisToolColorPicker::KisToolColorPicker(KoCanvasBase* canvas)
    :  KisTool(canvas, KisCursor::pickerCursor())
{
    setObjectName("tool_colorpicker");
    m_optionsWidget = 0;
    m_radius = 1;
    m_addPalette = false;
    m_updateColor = true;
    m_normaliseValues = false;
    m_pickedColor = KoColor();
}

KisToolColorPicker::~KisToolColorPicker()
{
}


void KisToolColorPicker::paint(QPainter& gc, const KoViewConverter &converter)
{
    Q_UNUSED(gc);
    Q_UNUSED(converter);
}

void KisToolColorPicker::mousePressEvent(KoPointerEvent *event)
{
    if (m_canvas) {
        if (event->button() != Qt::LeftButton && event->button() != Qt::RightButton)
            return;

        if (!currentImage())
            return;

        KisPaintDeviceSP dev = currentLayer()->paintDevice();

        if (!dev) return;

        bool sampleMerged = m_optionsWidget->cmbSources->currentIndex() == SAMPLE_MERGED;
        if (!sampleMerged) {
            if (!currentLayer())
            {
                KMessageBox::information(0, i18n("Cannot pick a color as no layer is active."));
                return;
            }
            if (!currentLayer()-> visible()) {
                KMessageBox::information(0, i18n("Cannot pick a color as the active layer is not visible."));
                return;
            }
        }

        QPoint pos = convertToIntPixelCoord(event);

        if (!currentImage()->bounds().contains(pos)) {
            return;
        }

        if (sampleMerged) {
            dev = currentImage()->mergedImage();
        }

        if (m_radius == 1) {
            m_pickedColor = dev->colorAt (pos.x(), pos.y());
        } else {
            // radius 2 ==> 9 pixels, 3 => 9 pixels, etc
            static int counts[] = { 0, 1, 9, 25, 45, 69, 109, 145, 193, 249 };

            KoColorSpace* cs = dev->colorSpace();
            int pixelSize = cs->pixelSize();

            quint8* data = new quint8[pixelSize];
            quint8** pixels = new quint8*[counts[m_radius]];
            quint8* weights = new quint8[counts[m_radius]];

            int i = 0;
            // dummy init
            KisHLineConstIteratorPixel iter = dev->createHLineConstIterator(0, 0, 1);;
            for (int y = - m_radius; y <= m_radius; y++) {
                for (int x = - m_radius; x <= m_radius; x++) {
                    if (x*x + y*y < m_radius * m_radius) {
                        iter = dev->createHLineIterator(pos.x() + x, pos.y() + y, 1);

                        pixels[i] = new quint8[pixelSize];
                        memcpy(pixels[i], iter.rawData(), pixelSize);

                        if (x == 0 && y == 0) {
                            // Because the sum of the weights must be 255,
                            // we cheat a bit, and weigh the center pixel differently in order
                            // to sum to 255 in total
                            // It's -(counts -1), because we'll add the center one implicitly
                            // through that calculation
                            weights[i] = 255 - (counts[m_radius]-1) * (255 / counts[m_radius]);
                        } else {
                            weights[i] = 255 / counts[m_radius];
                        }
                        i++;
                    }
                }
            }
            // Weird, I can't do that directly :/
            const quint8** cpixels = const_cast<const quint8**>(pixels);
            cs->mixColorsOp()->mixColors(cpixels, weights, counts[m_radius], data);
            m_pickedColor = KoColor(data, cs);

            for (i = 0; i < counts[m_radius]; i++)
                delete[] pixels[i];
            delete[] pixels;
            delete[] data;
        }

        displayPickedColor();

        if (m_updateColor) {
            if (event->button() == Qt::LeftButton)
                m_canvas->resourceProvider()->setResource(KoCanvasResource::ForegroundColor, m_pickedColor);
            else
                m_canvas->resourceProvider()->setResource(KoCanvasResource::BackgroundColor, m_pickedColor);
        }

        if (m_addPalette) {
            KoColorSetEntry ent;
            ent.color = m_pickedColor;
            // We don't ask for a name, too intrusive here

            KoColorSet* palette = m_palettes.at(m_optionsWidget->cmbPalette->currentIndex());
            palette->add(ent);

            if (!palette->save()) {
                KMessageBox::error(0, i18n("Cannot write to palette file %1. Maybe it is read-only.", palette->filename()), i18n("Palette"));
            }
        }
    }
}

void KisToolColorPicker::mouseMoveEvent(KoPointerEvent *event)
{
    Q_UNUSED(event);
}

void KisToolColorPicker::mouseReleaseEvent(KoPointerEvent *event)
{
    Q_UNUSED(event);
}

void KisToolColorPicker::displayPickedColor()
{
    if (m_pickedColor.data() && m_optionsWidget) {

        QList<KoChannelInfo *> channels = m_pickedColor.colorSpace()->channels();
        m_optionsWidget->listViewChannels->clear();

        for (int i = 0; i < channels.count(); ++i) {
            QString channelValueText;

            if (m_normaliseValues) {
                channelValueText = QString(i18n("%1%")).arg(m_pickedColor.colorSpace()->normalisedChannelValueText(m_pickedColor.data(), i));
            } else {
                channelValueText = m_pickedColor.colorSpace()->channelValueText(m_pickedColor.data(), i);
            }

            QTreeWidgetItem *item = new QTreeWidgetItem(m_optionsWidget->listViewChannels);
            item->setText(0, channels[i]->name());
            item->setText(1, channelValueText);
        }
    }
}

QWidget* KisToolColorPicker::createOptionWidget()
{
    m_optionsWidget = new ColorPickerOptionsWidget(0);

    m_optionsWidget->cbUpdateCurrentColor->setChecked(m_updateColor);

    m_optionsWidget->cmbSources->setCurrentIndex(0);

    m_optionsWidget->cbNormaliseValues->setChecked(m_normaliseValues);
    m_optionsWidget->cbPalette->setChecked(m_addPalette);
    m_optionsWidget->radius->setValue(m_radius);

    m_optionsWidget->listViewChannels->setSortingEnabled(false);

    connect(m_optionsWidget->cbUpdateCurrentColor, SIGNAL(toggled(bool)), SLOT(slotSetUpdateColor(bool)));
    connect(m_optionsWidget->cbNormaliseValues, SIGNAL(toggled(bool)), SLOT(slotSetNormaliseValues(bool)));
    connect(m_optionsWidget->cbPalette, SIGNAL(toggled(bool)),
            SLOT(slotSetAddPalette(bool)));
    connect(m_optionsWidget->radius, SIGNAL(valueChanged(int)),
            SLOT(slotChangeRadius(int)));

    KisResourceServerBase* srv = KisResourceServerRegistry::instance()->value("PaletteServer");

    if (!srv) {
        return m_optionsWidget;
    }

    QList<KoResource*> palettes = srv->resources();

    foreach (KoResource *resource, palettes) {
        KoColorSet* palette = dynamic_cast<KoColorSet*>(resource);
        if (palette) {
            m_optionsWidget->cmbPalette->addItem(palette->name());
            m_palettes.append(palette);
        }
    }

    connect(srv, SIGNAL(resourceAdded(KoResource*)), this, SLOT(slotAddPalette(KoResource*)));

    return m_optionsWidget;
}

QWidget* KisToolColorPicker::optionWidget()
{
    return m_optionsWidget;
}

void KisToolColorPicker::slotSetUpdateColor(bool state)
{
    m_updateColor = state;
}


void KisToolColorPicker::slotSetNormaliseValues(bool state)
{
    m_normaliseValues = state;
    displayPickedColor();
}

void KisToolColorPicker::slotSetAddPalette(bool state) {
    m_addPalette = state;
}

void KisToolColorPicker::slotChangeRadius(int value) {
    m_radius = value;
}

void KisToolColorPicker::slotAddPalette(KoResource* resource) {
    KoColorSet* palette = dynamic_cast<KoColorSet*>(resource);
    if (palette) {
        m_optionsWidget->cmbPalette->addItem(palette->name());
        m_palettes.append(palette);
    }
}

