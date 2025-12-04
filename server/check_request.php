<?php
/*
 * OpenAI Proxy Server with context window and web search for FujiNet AI SAM App
 * 
 * Copyright (c) 2025 Joe Honold, mozzwald <gmail.com>
 * 
 * GPL v3 License
 * ------------- check_request.php
 * Called by the FujiNet client to poll for completion of an AI request.
 * Validates token_id, ensures message ownership, and returns response
 * once the assistant's message is marked complete.
 *
 */

include_once "includes.php";

// Expect GET parameters: message_id and token_id
$message_id = $_GET['message_id'] ?? null;
$token_id   = $_GET['token_id'] ?? null;

if (!$message_id || !$token_id) {
    http_response_code(400);
    echo json_encode(["error" => "Missing required parameters"]);
    exit;
}

// Connect to database
try {
    $dsn = "mysql:host={$dbhost};dbname={$dbname};charset=utf8mb4";
    $pdo = new PDO($dsn, $dbuser, $dbpass, [
        PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
        PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
    ]);
} catch (PDOException $e) {
    http_response_code(500);
    echo json_encode(["error" => "Database connection failed"]);
    exit;
}

// Validate token_id exists
$stmt = $pdo->prepare("SELECT token_id FROM tokens WHERE token_id = ?");
$stmt->execute([$token_id]);
if ($stmt->rowCount() === 0) {
    http_response_code(403);
    echo json_encode(["error" => "Invalid token"]);
    exit;
}

// Validate message belongs to this token
$stmt = $pdo->prepare("SELECT content, status FROM messages WHERE id = ? AND token_id = ? AND role = 'assistant'");
$stmt->execute([$message_id, $token_id]);
$row = $stmt->fetch();

if (!$row) {
    http_response_code(404);
    echo json_encode([
        "token_id" => $token_id,
        "error" => "Message not found or does not belong to this token"]
    );
    exit;
}

if ((int)$row['status'] === 1) {
    // Still pending
    echo json_encode([
        "token_id" => $token_id,
        "status" => "pending"]
    );
    exit;
}

// Decode stored JSON blob: {"text_display":"...","text_sam":"..."}
$payload = json_decode($row['content'], true);

if (!is_array($payload)) {
    // Fallback if DB content wasn't a JSON blob
    $display = convert_atascii($row['content']);
    echo json_encode([
        "token_id"     => $token_id,
        "status"       => "complete",
        "text_display" => $display,
        "text_sam"     => $display
    ]);
    exit;
}

// Enforce device rules & sanitize
$display = isset($payload['text_display']) ? convert_atascii($payload['text_display']) : '';
$sam     = isset($payload['text_sam'])     ? convert_atascii($payload['text_sam'])     : '';

if (strlen($display) > 960) $display = substr($display, 0, 960);

echo json_encode([
    "token_id"     => $token_id,
    "status"       => "complete",
    "text_display" => $display,
    "text_sam"     => $sam
]);
exit;
?>
