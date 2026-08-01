#pragma once

 static char* methods[] = { 
    "GET",                 
    "POST",                
    "PUT",                 
    "PATCH",               
    "HEAD",                
    "OPTIONS",             
    "TRACE",               
    "DELETE",              
    "CONNECT",             
    "PRI",                 
    "ACL",                 
    "COPY",                
    "LOCK",                
    "LINK",                
    "MOVE",                
    "BIND",                
    "MERGE",               
    "MKCOL",               
    "LABEL",              
    "REPORT",              
    "SEARCH",              
    "UNBIND",              
    "UNLINK",              
    "UNLOCK",              
    "UPDATE",              
    "CHECKIN",             
    "PROPFIND",            
    "CHECKOUT",            
    "PROPPATCH",           
    "MKACTIVITY",          
    "MKCALENDAR",          
    "ORDERPATCH",          
    "UNCHECKOUT",          
    "MKWORKSPACE",         
    "MKREDIRECTREF",       
    "VERSION-CONTROL",     
    "BASELINE-CONTROL",    
    "UPDATE-REDIRECT-REF"  
 };


static const uint16_t codes[] = { 
    /* 1xx - Information */
    100, 101, 102, 103,

    /* 2xx - Succès */
    200, 201, 202, 203, 204, 205, 206, 207, 208, 226,

    /* 3xx - Redirection */
    300, 301, 302, 303, 304, 305, 306, 307, 308,

    /* 4xx - Erreur client */
    400, 401, 402, 403, 404, 405, 406, 407, 408, 409,
    410, 411, 412, 413, 414, 415, 416, 417, 418, 421,
    422, 423, 424, 425, 426, 428, 429, 431, 451,

    /* 5xx - Erreur serveur */
    500, 501, 502, 503, 504, 505, 506, 507, 508, 510, 511
};
