<?php
/*
 * OpenAI Proxy Server with context window and web search for FujiNet AI SAM App
 * 
 * Copyright (c) 2025 Joe Honold, mozzwald <gmail.com>
 * 
 * GPL v3 License
 * ------------- submit_request.php 
 * Handles authenticated message submission, async OpenAI request spawning,
 * and immediate response with message tracking ID.
 *
 */

include_once "includes.php";

// Only accept POST requests
if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    http_response_code(405);
    echo json_encode(["error" => "Method Not Allowed"]);
    exit;
}

// Read JSON input
$inputJSON = file_get_contents("php://input");
$decodedInput = json_decode($inputJSON, true);

// Validate JSON
if (!$decodedInput) {
    http_response_code(400);
    if ($log_errors)
        file_put_contents($log_file, date("[Y-m-d H:i:s]") . " Invalid JSON:\n" . $inputJSON . "\n\n", FILE_APPEND);
    echo json_encode(["error" => "Invalid JSON input"]);
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

// Handle new token request
if (isset($decodedInput['new']) && $decodedInput['new'] === $defaultKey) {
    if (isset($decodedInput['token_id']) && $decodedInput['token_id'] !== $defaultKey) {
        $oldToken = $decodedInput['token_id'];
        $stmt = $pdo->prepare("DELETE FROM messages WHERE token_id = ?");
        $stmt->execute([$oldToken]);
        $stmt = $pdo->prepare("DELETE FROM tokens WHERE token_id = ?");
        $stmt->execute([$oldToken]);
    }

    $newToken = bin2hex(random_bytes(16));
    $stmt = $pdo->prepare("INSERT INTO tokens (token_id) VALUES (?)");
    $stmt->execute([$newToken]);

    echo json_encode(["token_id" => $newToken]);
    exit;
}

// Verify token
if (!isset($decodedInput['token_id'])) {
    echo json_encode(["error" => "Missing token_id"]);
    exit;
}

$token_id = $decodedInput['token_id'];
$stmt = $pdo->prepare("SELECT token_id FROM tokens WHERE token_id = ?");
$stmt->execute([$token_id]);
if ($stmt->rowCount() === 0) {
    echo json_encode([
        "token_id" => $token_id,
        "error" => "Invalid token"
    ]);
    exit;
}

// Validate message
if (empty($decodedInput['message'])) {
    echo json_encode([
        'token_id' => $token_id,
        "error" => "Missing message"
    ]);
    exit;
}

// Trim
$message = trim($decodedInput['message']);

// Remove any control characters except newline
$message = preg_replace('/[^\r\n\x20-\x7E]/', '', $message);

// Collapse multiple whitespace/newlines to single space or newline
$message = preg_replace([ "/[ \t]{2,}/", "/\r?\n{2,}/" ], [ ' ', "\n" ], $message);

// Enforce a reasonable length limit
$maxLen = 2048;
if (strlen($message) > $maxLen) {
    $message = substr($message, 0, $maxLen);
}

// Reject if empty after cleaning
if ($message === '') {
    http_response_code(400);
    header('Content-Type: application/json');
    echo json_encode([
        'token_id' => $token_id,
        'error'    => 'Content is empty or contains only invalid characters'
    ]);
    exit;
}

// Insert user's message
$stmt = $pdo->prepare("INSERT INTO messages (token_id, role, content, status) VALUES (?, 'user', ?, 0)");
$stmt->execute([$token_id, $message]);

// Insert placeholder assistant message (pending)
$stmt = $pdo->prepare("INSERT INTO messages (token_id, role, content, status) VALUES (?, 'assistant', '', 1)");
$stmt->execute([$token_id]);
$assistant_id = $pdo->lastInsertId();

if ($log_errors) file_put_contents($log_file, date("[Y-m-d H:i:s]") . " User Request (token_id ".$token_id."):\n  {".$message."}\n", FILE_APPEND);

// Spawn background worker for OpenAI request
exec("php process_request.php $assistant_id > /dev/null 2>&1 &");

// Spawn background worker for database cleanup. Only actually runs once a day
exec("php cleanup_tokens.php > /dev/null 2>&1 &");

// Respond immediately
echo json_encode([
    'token_id' => $token_id,
    "message_id" => $assistant_id,
    "status" => "pending"
]);
exit;
?>
