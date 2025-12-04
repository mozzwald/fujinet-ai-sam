<?php
/*
 * OpenAI Proxy Server and context window and web search for FujiNet AI SAM App
 * 
 * Copyright (c) 2025 Joe Honold, mozzwald <gmail.com>
 * 
 * GPL v3 License
 * ------------- includes.php 
 * - Settings for the AI SAM API
 */

// OpenAI API key
$API_KEY = "YOUR_API_KEY";
// Default app Token / Key. used as sort of a password to get a unique token for the first time. sent by app
$defaultKey = "YOUR_DEFAULT_TOKEN_TO_MATCH_APP_CONFIG_H";

// Database Variables
$dbhost = "127.0.0.1";
$dbuser = "YOUR_DB_USERNAME";
$dbpass = 'YOUR_DB_PASSWORD';
$dbname = "ai-sam";

// How many messages to keep saved and send to API
$historyLimit = 9;

// Default retention: number of days to keep tokens + messages
$daysLimit = 7;

// Log File
$log_errors = 0; // 1 = yes to log them in a file, 0 = no
$log_file = "ai-sam-api.log";
$debug = 0; // Extra debug logging

/**
 * Convert ASCII text to ATASCII
 */
function convert_atascii($str)
{
    // First convert to ASCII
    $str = convert_ascii($str);

	$newtext = "";
	for ( $pos=0; $pos < strlen($str); $pos ++ )
	{
		$byte = substr($str, $pos);
		if(ord($byte) == 10) // Replace $0A with SPACE
			$newtext .= chr(32);
		else
			$newtext .= chr(ord($byte));
	}
	return $newtext;
}

/**
 * Remove any non-ASCII characters and convert known non-ASCII characters
 * to their ASCII equivalents, if possible.
 *
 * @param string $string
 * @return string $string
 * @author Jay Williams <myd3.com>
 * @license MIT License
 * @link http://gist.github.com/119517
 */
function convert_ascii($string)
{
    // Replace Single Curly Quotes
    $search[]  = chr(226) . chr(128) . chr(152);
    $replace[] = "'";
    $search[]  = chr(226) . chr(128) . chr(153);
    $replace[] = "'";

    // Replace Smart Double Curly Quotes
    $search[]  = chr(226) . chr(128) . chr(156);
    $replace[] = '"';
    $search[]  = chr(226) . chr(128) . chr(157);
    $replace[] = '"';

    // Replace En Dash
    $search[]  = chr(226) . chr(128) . chr(147);
    $replace[] = '--';

    // Replace Em Dash
    $search[]  = chr(226) . chr(128) . chr(148);
    $replace[] = '---';

    // Replace Bullet
    $search[]  = chr(226) . chr(128) . chr(162);
    $replace[] = '*';

    // Replace Middle Dot
    $search[]  = chr(194) . chr(183);
    $replace[] = '*';

    // Replace Ellipsis with three consecutive dots
    $search[]  = chr(226) . chr(128) . chr(166);
    $replace[] = '...';

    // Apply Replacements
    $string    = str_replace($search, $replace, $string);

    // Remove any non-ASCII Characters
    $string    = preg_replace("/[^\x01-\x7F]/", "", $string);

    return $string;
}

?>