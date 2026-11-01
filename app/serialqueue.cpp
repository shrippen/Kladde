#include "serialqueue.h"

SerialQueue::SerialQueue(QObject *parent)
    : QObject(parent)
{
    m_thread.setMaxThreadCount(1);
}
