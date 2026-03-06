<?php
header('Content-Type: application/json');
require '../db.php';

if ($_SERVER['REQUEST_METHOD'] == 'POST' || isset($_GET['sign_id'])) {
    $sign_id = $_POST['sign_id'] ?? $_GET['sign_id'] ?? null;
    $confidence = $_POST['confidence'] ?? $_GET['confidence'] ?? 0.0;

    if ($sign_id) {
        try {
            $stmt = $pdo->prepare("INSERT INTO detections (id_traffic_sign, confidence) VALUES (?, ?)");
            $stmt->execute([$sign_id, $confidence]);
            
            // Fetch sign info for immediate context
            $stmt = $pdo->prepare("SELECT ts.name, st.name_type FROM traffic_signs ts JOIN sign_type st ON ts.id_sign_type = st.id_sign_type WHERE ts.id_traffic_signs = ?");
            $stmt->execute([$sign_id]);
            $sign = $stmt->fetch();

            echo json_encode([
                "status" => "success",
                "message" => "Detection logged",
                "data" => $sign
            ]);
        } catch (Exception $e) {
            echo json_encode(["status" => "error", "message" => $e->getMessage()]);
        }
    } else {
        echo json_encode(["status" => "error", "message" => "sign_id required"]);
    }
}
?>
