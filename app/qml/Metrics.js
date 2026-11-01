.pragma library

// Names of HealthLog's metric kinds. The server sends i18n keys meant for its own
// bundle; the app names the kinds itself, in its own translations.
function title(kind) {
    switch (kind) {
    case "steps": return qsTr("Steps")
    case "sleep": return qsTr("Sleep")
    case "pulse": return qsTr("Pulse")
    case "restingHeartRate": return qsTr("Resting heart rate")
    case "heartRateVariability": return qsTr("Heart rate variability")
    case "weight": return qsTr("Weight")
    case "bloodPressure": return qsTr("Blood pressure")
    case "bodyFat": return qsTr("Body fat")
    case "glucose": return qsTr("Glucose")
    case "totalBodyWater": return qsTr("Body water")
    case "boneMass": return qsTr("Bone mass")
    case "oxygenSaturation": return qsTr("Oxygen saturation")
    case "mood": return qsTr("Mood")
    case "bmi": return qsTr("BMI")
    }
    return kind
}

// Which way is good when the server does not say: 1 up, -1 down, 0 neither.
function goodDirection(kind) {
    switch (kind) {
    case "steps": case "sleep": case "mood": return 1
    case "pulse": case "restingHeartRate": case "bloodPressure": return -1
    }
    return 0
}
