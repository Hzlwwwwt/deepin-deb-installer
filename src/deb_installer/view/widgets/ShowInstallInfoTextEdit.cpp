/*
* Copyright (C) 2019 ~ 2020 Uniontech Software Technology Co.,Ltd
*
* Author:     cuizhen <cuizhen@uniontech.com>
* Maintainer:  cuizhen <cuizhen@uniontech.com>
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* any later version.
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "ShowInstallInfoTextEdit.h"

#include <QAbstractTextDocumentLayout>
#include <QTextDocumentFragment>
#include <QScrollBar>
#include <QEvent>
#include <QDebug>

ShowInstallInfoTextEdit::ShowInstallInfoTextEdit(QWidget *parent):
    QTextEdit(parent)
{
    setAttribute(Qt::WA_AcceptTouchEvents);
    grabGesture(Qt::TapGesture);                        //获取触控点击事件
    grabGesture(Qt::TapAndHoldGesture);                 //获取触控点击长按事件

    //滑动鼠标时选中的效果
    connect(this, &ShowInstallInfoTextEdit::selectionChanged, this, &ShowInstallInfoTextEdit::onSelectionArea);
}

/**
 * @brief ShowInstallInfoTextEdit::onSelectionArea 选中文字
 */
void ShowInstallInfoTextEdit::onSelectionArea()
{
    if (m_gestureAction != GA_null) {
        QTextCursor cursor = textCursor();
        if (cursor.selectedText() != "") {
            cursor.clearSelection();
            setTextCursor(cursor);
        }
    }
}

/**
 * @brief ShowInstallInfoTextEdit::event 事件处理
 * @param event
 * @return
 */
bool ShowInstallInfoTextEdit::event(QEvent *event)
{
    if (event->type() == QEvent::Gesture)
        gestureEvent(static_cast<QGestureEvent *>(event));
    return QTextEdit::event(event);
}

/**
 * @brief ShowInstallInfoTextEdit::gestureEvent 手势事件的处理
 * @param event
 * @return
 */
bool ShowInstallInfoTextEdit::gestureEvent(QGestureEvent *event)
{
    if (QGesture *tap = event->gesture(Qt::TapGesture))
        tapGestureTriggered(static_cast<QTapGesture *>(tap));
    if (QGesture *tapAndHold = event->gesture(Qt::TapAndHoldGesture))
        tapAndHoldGestureTriggered(static_cast<QTapAndHoldGesture *>(tapAndHold));
    return true;
}

/**
 * @brief ShowInstallInfoTextEdit::tapGestureTriggered 单指点击事件的处理
 * @param tap
 */
void ShowInstallInfoTextEdit::tapGestureTriggered(QTapGesture *tap)
{
    this->clearFocus();
    //单指点击函数
    switch (tap->state()) {             //根据点击的状态进行不同的操作
    case Qt::GestureStarted: {          //开始点击，记录时间。时间不同 进行不同的操作
        m_gestureAction = GA_tap;
        m_tapBeginTime = QDateTime::currentDateTime().toMSecsSinceEpoch();
        break;
    }
    case Qt::GestureUpdated: {
        m_gestureAction = GA_slide;     //触控滑动
        break;
    }
    case Qt::GestureCanceled: {
        //根据时间长短区分轻触滑动
        qint64 timeSpace = QDateTime::currentDateTime().toMSecsSinceEpoch() - m_tapBeginTime;
        if (timeSpace < TAP_MOVE_DELAY || m_slideContinue) {    //普通滑动
            m_slideContinue = false;
            m_gestureAction = GA_slide;
        } else {                                                //选中滑动
            m_gestureAction = GA_null;
        }
        break;
    }
    case Qt::GestureFinished: {
        m_gestureAction = GA_null;
        break;
    }
    default: {
        Q_ASSERT(false);
        break;
    }
    }
}

/**
 * @brief ShowInstallInfoTextEdit::tapAndHoldGestureTriggered 单指长按事件
 * @param tapAndHold
 */
void ShowInstallInfoTextEdit::tapAndHoldGestureTriggered(QTapAndHoldGesture *tapAndHold)
{
    //单指长按
    switch (tapAndHold->state()) {
    case Qt::GestureStarted:
        m_gestureAction = GA_hold;
        break;
    case Qt::GestureUpdated:
        Q_ASSERT(false);
        break;
    case Qt::GestureCanceled:
        Q_ASSERT(false);
        break;
    case Qt::GestureFinished:
        m_gestureAction = GA_null;
        break;
    default:
        Q_ASSERT(false);
        break;
    }
}

/**
 * @brief ShowInstallInfoTextEdit::slideGesture 滑动事件的处理
 * @param diff
 */
void ShowInstallInfoTextEdit::slideGesture(qreal diff)
{
    static qreal delta = 0.0;
    int step = static_cast<int>(diff + delta);
    delta = diff + delta - step;
    verticalScrollBar()->setValue(verticalScrollBar()->value() + step);         //移动滚动条
}

/**
 * @brief ShowInstallInfoTextEdit::mouseReleaseEvent 鼠标释放事件
 * @param e
 */
void ShowInstallInfoTextEdit::mouseReleaseEvent(QMouseEvent *e)
{
    change = 0.0;
    duration = 0.0;
    //add for single refers to the sliding
    if (e->type() == QEvent::MouseButtonRelease && e->source() == Qt::MouseEventSynthesizedByQt) {

        if (m_gestureAction == GA_slide) {
            //滑动结束，开始惯性滑动
            tween.start(0, 0, change, duration, std::bind(&ShowInstallInfoTextEdit::slideGesture, this, std::placeholders::_1));
        }
        m_gestureAction = GA_null;
    }

    int i = m_end - m_start;
    if (Qt::MouseEventSynthesizedByQt == e->source()
            && (i > 10 && this->verticalScrollBar()->value() != 0)) {
        e->accept();
        return;
    }
    return QTextEdit::mouseReleaseEvent(e);
}

/**
 * @brief ShowInstallInfoTextEdit::mouseMoveEvent 鼠标移动事件
 * @param e
 */
void ShowInstallInfoTextEdit::mouseMoveEvent(QMouseEvent *e)
{
    if (Qt::MouseEventSynthesizedByQt == e->source()) {
        m_end = e->y();
    }

    //单指滑动
    if (e->type() == QEvent::MouseMove && e->source() == Qt::MouseEventSynthesizedByQt) {
        const ulong diffTime = e->timestamp() - m_lastMouseTime;
        const int diffpos = e->pos().y() - m_lastMousepos;
        m_lastMouseTime = e->timestamp();
        m_lastMousepos = e->pos().y();

        if (m_gestureAction == GA_slide) {
            QFont font = this->font();

            /*开根号时数值越大衰减比例越大*/
            qreal direction = diffpos < 0 ? 1.0 : -1.0;
            slideGesture(-direction * sqrt(abs(diffpos)) / font.pointSize());

            /*预算惯性滑动时间*/
            m_stepSpeed = static_cast<qreal>(diffpos) / static_cast<qreal>(diffTime + 0.000001);
            duration = sqrt(abs(m_stepSpeed)) * 1000;

            /*预算惯性滑动距离,4.0为调优数值*/
            m_stepSpeed /= sqrt(font.pointSize() * 4.0);
            change = m_stepSpeed * sqrt(abs(m_stepSpeed)) * 100;
        }
        return;
    }
    QTextEdit::mouseMoveEvent(e);
}

/**
 * @brief FlashTween::FlashTween 惯性滑动
 */
FlashTween::FlashTween()
{
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &FlashTween::__run);
}

/**
 * @brief FlashTween::start 开始滑动
 * @param t
 * @param b
 * @param c
 * @param d
 * @param f
 */
void FlashTween::start(qreal t, qreal b, qreal c, qreal d, FunSlideInertial f)
{
    if (c == 0.0 || d == 0.0) return;
    m_currentTime = t;
    m_beginValue = b;
    m_changeValue = c;
    m_durationTime = d;

    m_lastValue = 0;
    m_fSlideGesture = f;
    m_direction = m_changeValue > 0 ? 1 : -1;

    m_timer->stop();
    m_timer->start(CELL_TIME);
}

/**
 * @brief FlashTween::__run 触发滑动
 */
void FlashTween::__run()
{
    qreal tempValue = m_lastValue;
    m_lastValue = FlashTween::sinusoidalEaseOut(m_currentTime, m_beginValue, abs(m_changeValue), m_durationTime);
    m_fSlideGesture(m_direction * (m_lastValue - tempValue));

    if (m_currentTime < m_durationTime) {
        m_currentTime += CELL_TIME;
    } else {
        m_timer->stop();
    }
}

