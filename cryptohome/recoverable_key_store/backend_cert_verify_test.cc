// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/recoverable_key_store/backend_cert_verify.h"

#include <optional>
#include <string>
#include <vector>

#include <base/base64.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace cryptohome {

namespace {

using ::testing::Each;
using ::testing::Field;
using ::testing::SizeIs;

// A valid cert xml from https://www.gstatic.com/cryptauthvault/v0/cert.xml
constexpr char kCertXmlB64[] =
    "PD94bWwgdmVyc2lvbj0iMS4wIiBlbmNvZGluZz0iVVRGLTgiPz4KPGNlcnRpZmljYXRlPgog"
    "IDxtZXRhZGF0YT4KICAgIDxzZXJpYWw+MTAwMTQ8L3NlcmlhbD4KICAgIDxjcmVhdGlvbi10"
    "aW1lPjE2ODQ5NjQ3NzI8L2NyZWF0aW9uLXRpbWU+CiAgICA8cmVmcmVzaC1pbnRlcnZhbD4y"
    "NTkyMDAwPC9yZWZyZXNoLWludGVydmFsPgogICAgPHByZXZpb3VzPgogICAgICA8c2VyaWFs"
    "PjEwMDEzPC9zZXJpYWw+CiAgICAgIDxoYXNoPmtWUHpIRjdJRFM0SDhNYzFzZ0lJY09JTmRW"
    "dkdKWFR2SGhvN3creWE3OE09PC9oYXNoPgogICAgPC9wcmV2aW91cz4KICA8L21ldGFkYXRh"
    "PgogIDxpbnRlcm1lZGlhdGVzPgogICAgPGNlcnQ+TUlJRkNqQ0NBdktnQXdJQkFnSVJBTjdk"
    "MUluT2pXR1RVVDU1OHpXUEx3RXdEUVlKS29aSWh2Y05BUUVMQlFBd0lERWVNQndHQTFVRUF4"
    "TVZSMjl2WjJ4bElFTnllWEIwUVhWMGFGWmhkV3gwTUI0WERURTRNRFV3T1RBeE1qQXdObG9Y"
    "RFRJNE1EVXhNREF4TWpBd05sb3dPVEUzTURVR0ExVUVBeE11UjI5dloyeGxJRU5zYjNWa0lF"
    "dGxlU0JXWVhWc2RDQlRaWEoyYVdObElFbHVkR1Z5YldWa2FXRjBaU0JEUVRDQ0FpSXdEUVlK"
    "S29aSWh2Y05BUUVCQlFBRGdnSVBBRENDQWdvQ2dnSUJBTzkwNjd4OTQrc3hJcHFYSE45cmNk"
    "b3JxVnNIOHMzUk9aZUJJM09SQVdNOGRHbVIrbS95ZzdycmNMckxaTkNLTW81UnNrakFjLzl0"
    "V0lGbm95SnZwM2JnSmFaTzFtT1pHQjZkRjFyYzNac1daSjVsazZyb0QzaldYb2loSTZBNXFj"
    "aUcyT2pmbjlkNFVOa1ZZZmxnMHhLTUVQNHRPRmdTKytYSWJJWlNCdnR3T05vT1VLK3cyUkNu"
    "VS9hQ1VLcEo3YzQ5SEJzaWVWL0FjSTNrNGlhNzJKTmlwLzlPZWVmeXFhZXVSdDBYOXZWVHox"
    "TjR1dTVMWVFFOTBtcnl3YVI5TjB1Rm1ma0pYNndJaGtNNHNuYmMvYmU1a3BOY1huNDJzZVdW"
    "Z0xpUUh3bXlueU4xVmdIR2xLK0QrZXdjNWczRW90STRMTldqTjdkZ2F6M3dERWNWcjkrY2cy"
    "WjZ3dmg0cWM1SThneGdYeDVoWUtJSmNvWFBYdnlvOTVrcnJEdEVhdGNJTGxWeXJOb1NsMGFH"
    "aGliaDdYdDJDTUV3dGFTODU2cjZKWVE5Wno2RjMvS3pNNEIwYzVYUFIvSWw3SUFkYWUvZSta"
    "NGVWZ2o2ekExOW5nSm1IV3RNVXpISEUzZ2N5RE5xSWNVTE1aWWVhN0kxMVRWTjRvVzFwQjZy"
    "c3lJc0JYQUxaWFQ5M1RKTEk5SFovdzUyQThxSkl4SUZQODlpTnRlaFBkOGZZWmlwQkpPajZl"
    "NlBMZjgrcGNERS9SU1NMczZlelVSSjFna292bnViTmhPeFE0K2t1OFdOc3hDRkI2NXNMcmlY"
    "Tkk4eVo4SFdmdEpzb3AyazVnUTd3VjBlWEZOWEpoQUdhSVhnZ0tFYi9XZitxQUVuTXl4ZEF1"
    "THJsWHdPUmwzQUp0ZUhBZ01CQUFHakpqQWtNQTRHQTFVZER3RUIvd1FFQXdJQmhqQVNCZ05W"
    "SFJNQkFmOEVDREFHQVFIL0FnRUJNQTBHQ1NxR1NJYjNEUUVCQ3dVQUE0SUNBUUJsYldjWGdE"
    "NEtDQmdCcE5VNno4Njc1b0FpSmI0WXdySThHVDJZNWxnbHo2amtteTlnUFpkVTU2UFB5WE8w"
    "TUlCQ3NtbVh4RWNWVVJEVUx1WDhESnNienVxbmJNOG1FYm1LOENWbE1ocTlOTk9GWk1DdG5o"
    "dTY0N2xZK1phYkJVWXI0YlNnUGlKeHd3TW9yM2MxNVBGeC9kZVpBWWVBdGJWOXpXMFEwN3lY"
    "bWpPb1FodGd2SmpFTzlwd3h3ZjFna3REOVdiajdPcFNpTE5sS0dwTEZPVGptMGNreklCR2d3"
    "dllXcCtBNkxDam1PenVWOTFoZFVGNExFckcwWjZHUVZsbGF6SFNKNW9hTkVKeDZ3eUpudCtn"
    "TDRURFh3Z0RGN1Fwa1NpeEJnZng1VFk5UVZzVGkvd0x6a0RDamw4eHVYM1lYZGxvam9ma3N4"
    "YTgzTUFGNlc4UHVhNFpoS0ZUY25HQUZRTVRmUE1VdDBCQUVreVR4bEFvdlo3SCtaWENrRDQ3"
    "VGtjR0k5S1dhdjdkREw5UDRJcVFsakQ5ZnIvUjBhbmxIK3J3Sm45akoxVXFUYldvSGdZcjhx"
    "TmE0U2tEM1dmWmhiN1RRSmJVRDZWb2NyRXFCejZQOVdnSkZsQjBObjU0dWU3UmxGQzUrbmxW"
    "OG02WlBiZjYrZjd3Vk9yVm4wT2J4cTJ0OVJTaUw5QWViUERnZnRzK0pndmZsbVBTT0hENVcr"
    "NG80MlM0L2h1ZWxmRnh1SU0xYWlkOGxaaXAwVEpCellYV21PQ3AyU1BIZE4wd0lwNy9tMUZq"
    "SjVaN3JqcW4wZEIrb1h2SGFweXdBZHltRWFWbS9yczk0MGQ1MGNHZy8xUmZ2QUMzb1lTeVpl"
    "OTlZZUs5REVRbzEyNDkrMG42UWhob0pRSkFDdz09PC9jZXJ0PgogICAgPGNlcnQ+TUlJRkdq"
    "Q0NBd0tnQXdJQkFnSVFIZmxuRE5Xa2oyeXhlRDFJQjZHZFRUQU5CZ2txaGtpRzl3MEJBUXNG"
    "QURBeE1TOHdMUVlEVlFRREV5WkhiMjluYkdVZ1EyeHZkV1FnUzJWNUlGWmhkV3gwSUZObGNu"
    "WnBZMlVnVW05dmRDQkRRVEFlRncweE9EQTFNRGN4T0RVNE1UQmFGdzB5T0RBMU1EZ3hPRFU0"
    "TVRCYU1Ea3hOekExQmdOVkJBTVRMa2R2YjJkc1pTQkRiRzkxWkNCTFpYa2dWbUYxYkhRZ1Uy"
    "VnlkbWxqWlNCSmJuUmxjbTFsWkdsaGRHVWdRMEV3Z2dJaU1BMEdDU3FHU0liM0RRRUJBUVVB"
    "QTRJQ0R3QXdnZ0lLQW9JQ0FRRHZkT3U4ZmVQck1TS2FseHpmYTNIYUs2bGJCL0xOMFRtWGdT"
    "TnprUUZqUEhScGtmcHY4b082NjNDNnkyVFFpaktPVWJKSXdIUC9iVmlCWjZNaWI2ZDI0Q1dt"
    "VHRaam1SZ2VuUmRhM04yYkZtU2VaWk9xNkE5NDFsNklvU09nT2FuSWh0am8zNS9YZUZEWkZX"
    "SDVZTk1TakJEK0xUaFlFdnZseUd5R1VnYjdjRGphRGxDdnNOa1FwMVAyZ2xDcVNlM09QUndi"
    "SW5sZndIQ041T0ltdTlpVFlxZi9Ubm5uOHFtbnJrYmRGL2IxVTg5VGVMcnVTMkVCUGRKcThz"
    "R2tmVGRMaFpuNUNWK3NDSVpET0xKMjNQMjN1WktUWEY1K05ySGxsWUM0a0I4SnNwOGpkVllC"
    "eHBTdmcvbnNIT1lOeEtMU09DelZvemUzWUdzOThBeEhGYS9mbklObWVzTDRlS25PU1BJTVlG"
    "OGVZV0NpQ1hLRnoxNzhxUGVaSzZ3N1JHclhDQzVWY3F6YUVwZEdob1ltNGUxN2RnakJNTFdr"
    "dk9lcStpV0VQV2MraGQveXN6T0FkSE9WejBmeUpleUFIV252M3ZtZUhsWUkrc3dOZlo0Q1po"
    "MXJURk14eHhONEhNZ3phaUhGQ3pHV0htdXlOZFUxVGVLRnRhUWVxN01pTEFWd0MyVjAvZDB5"
    "U3lQUjJmOE9kZ1BLaVNNU0JUL1BZamJYb1QzZkgyR1lxUVNUbytudWp5My9QcVhBeFAwVWtp"
    "N09uczFFU2RZSktMNTdtellUc1VPUHBMdkZqYk1RaFFldWJDNjRselNQTW1mQjFuN1NiS0tk"
    "cE9ZRU84RmRIbHhUVnlZUUJtaUY0SUNoRy8xbi9xZ0JKek1zWFFMaTY1VjhEa1pkd0NiWGh3"
    "SURBUUFCb3lZd0pEQU9CZ05WSFE4QkFmOEVCQU1DQVlZd0VnWURWUjBUQVFIL0JBZ3dCZ0VC"
    "L3dJQkFUQU5CZ2txaGtpRzl3MEJBUXNGQUFPQ0FnRUFRK0czdjNKQ2J6Q2hCczhIVUd4Nmky"
    "VE1tMU5aTTcxK2NoYkEySkY5RGU4a1ZkL3IyQ0VUdnZCUkxYY1RQY1dXQTArUFJER2FEbWk0"
    "VFIzYkpoWGdCU3RlY1Faa1F0ekkzWmNkRmZJMHJUTmVDZXZmSHA1bkpqdEIrQVlvbUNUS05y"
    "bE5McGs5WWJKb3NxRUtWTFFCaGxMTlltM1BUNENRWUoxTnViTEx0S0YxY240WitlYXl4dWQx"
    "a0RyWldGeU41Q1lld09ydFhjOG9DeW5qOEgwL055ZE91Q1JRVTJjL1VYV212c21sUlJmZkhK"
    "RVhMcUNNaXRUSFY5dzRWSEVWZzlZWXNzeG5vL2pXdHArYjR6OEpzRTJ2a0pqczJ0bU92ZmlN"
    "dXBiSng5aDZ6ajJqMDRyamhmL0ErdkdQUktPRDVXdGJiWDRBbjIrc3pzTkxtRVJCZldVTnNP"
    "MUFhU1RjM1crQUpPanJHMzB0ZXdTN2pGUlBsdVR0Z0Ira21velNXME1VL0JnQVlKdU5LUlZQ"
    "OHprbFZtUXFKUmJycnhTenJ2SHpKbHovbHZGdTlNRDduR3RpRnFUOVZnZ0ZqcXE1dmduNXNy"
    "QnAzRHE0R0RHZXJnK0hDRENOOXFnbkwxZ0JjS3pDTUsxb1QwYkNSV1pHY2tUMjhXTW5mY2da"
    "L2Z1RVZOZ1FjRVhMZ1dpWldaREJFVmxNaDd1MlFvT3IyTFh3WHVYTUU4azg3ckFRYnh2R0xo"
    "eXhxMnVOeFVkSDE2dWxqbTdwNXUyUW9ieXF4cWYyck9HSllDQkxLMkpQNzRkNk5sNmhENUZH"
    "QkJhTzZtTjBPam4vU2hKMUNxOW8zd0NIb0xZbjU1d0puWFl1N1FYQVg2MjMwaDdla1hwYnhQ"
    "UEhPNHgwVmFyNXArOD08L2NlcnQ+CiAgPC9pbnRlcm1lZGlhdGVzPgogIDxlbmRwb2ludHM+"
    "CiAgICA8Y2VydD5NSUlET2pDQ0FTS2dBd0lCQWdJUVRZWmFaM2ZBMWNBYStReUtJcGg1Z2pB"
    "TkJna3Foa2lHOXcwQkFRc0ZBREE1TVRjd05RWURWUVFERXk1SGIyOW5iR1VnUTJ4dmRXUWdT"
    "MlY1SUZaaGRXeDBJRk5sY25acFkyVWdTVzUwWlhKdFpXUnBZWFJsSUVOQk1CNFhEVEl6TURV"
    "eU16SXhORFl4TWxvWERUSTFNRFF3T1RBd01EQXdNRm93TWpFd01DNEdBMVVFQXhNblIyOXZa"
    "MnhsSUVOc2IzVmtJRXRsZVNCV1lYVnNkQ0JUWlhKMmFXTmxJRVZ1WkhCdmFXNTBNRmt3RXdZ"
    "SEtvWkl6ajBDQVFZSUtvWkl6ajBEQVFjRFFnQUVnSEJpMm1ZN3RqSlpLQkR6dVg3UEl3dFR3"
    "UXYxNXdONEZwaVo2dncrdDFla1VMemNaMkh6SXZtNHZ0ZEsyRVR1Z0I0d0F6Tk5tZFo2MTBI"
    "WkRKSURsNk1RTUE0d0RBWURWUjBUQVFIL0JBSXdBREFOQmdrcWhraUc5dzBCQVFzRkFBT0NB"
    "Z0VBTmdzNSswRkxlR0hYZmszNnZwVGtXWVh2VDFMY3RKdzBMQUYrcDRFcDl2TjNjQ25Tb0E0"
    "aFBhbzlnclM5S2ltR0hjTmVTRUxnNUMvUUc4eko1NnkrNGZ4b1ZOam9JSDdNSVVFTzRFUTNE"
    "a0UrWXpLUEFPZHM3WWo2dWd2YUh3UW16aHJidzlPd2RxOWRaNEUvcnFnYzdvWHg4N2VCVnE3"
    "a3UrWm92VG1JS05Ga2dZTWNmRXdsL1NkbExtYlpoU2VvRmQwN2htMHN4SDByQnByNEZYRHlI"
    "RUltN3RGenNWd0g4ODJPblVYekRnZStGcmNyQWJnZGVTK0NNZ3hlT2psL1V5UHhLcDdGQlQw"
    "bmtFMDJZM2IvckRwK0VrNW9ubU1WYXlmVDRkc3VvVWJDWWdja08veTdpOEg3WjZESXpPV3pG"
    "Ym9GWHpUTGhKVkNYUjRXSVViMkpHVWNaY0dkTmNRTnVCaEZlRXVBR2lDNHBVUVp0QU9VKzdL"
    "SjJPVnpTcnNidkJzVEx1ZGl0VG1hdGdDWE1XbklsN1R5YWlmVElncHBoUmZWMmZxYnplTzhU"
    "eXRWcXhSOUZVdm1QZjllYUtScFJlbFo3R0JVdjBHcU5kYVcrU0xZdVhiY25GWDRLbENhY253"
    "Mmkxd1BQTnNLWFBtWmVkUDVmTnVTUXNFei8ya1pmcHNUNjlEQzZUYmJZZ0RzU1l6QUZGVHdW"
    "OGN0YUF3Z1V2bDlGTHp2dkp6blNNNlh5ZStIVVJDWVFrdnY1UHBjVnVIZHhmNWxjZmttYnBl"
    "bCtwZDU1WTV4WWdDSS9JdzdDRmNIazBIeGl0ck1RNVdCdTZneUhzUjlNWnlFVTgxcm82NWdr"
    "cElqL2VOa2FmaDRhS3JBWDJZUlZiaFVXNTNzci9MMVQwdWphYkk9PC9jZXJ0PgogICAgPGNl"
    "cnQ+TUlJRE9qQ0NBU0tnQXdJQkFnSVFUWVphWjNmQTFjQWErUXlLSXBoNWdqQU5CZ2txaGtp"
    "Rzl3MEJBUXNGQURBNU1UY3dOUVlEVlFRREV5NUhiMjluYkdVZ1EyeHZkV1FnUzJWNUlGWmhk"
    "V3gwSUZObGNuWnBZMlVnU1c1MFpYSnRaV1JwWVhSbElFTkJNQjRYRFRJek1EVXlNekl4TkRZ"
    "eE1sb1hEVEkxTURRd09UQXdNREF3TUZvd01qRXdNQzRHQTFVRUF4TW5SMjl2WjJ4bElFTnNi"
    "M1ZrSUV0bGVTQldZWFZzZENCVFpYSjJhV05sSUVWdVpIQnZhVzUwTUZrd0V3WUhLb1pJemow"
    "Q0FRWUlLb1pJemowREFRY0RRZ0FFa21acFQ2TXlIYzNmK3JlQWEzN3Zld1E3WEJoelJlQW5Q"
    "Q0RRWURLMEFFVEtsZFFuWlRIVUJaUnZWaUJGUmtkSUI3WEdmcXlUcURBTXBna20wMTVjTTZN"
    "UU1BNHdEQVlEVlIwVEFRSC9CQUl3QURBTkJna3Foa2lHOXcwQkFRc0ZBQU9DQWdFQWVKeWZM"
    "VnNnUkQrUTJjNFZBbXMySjkySkZ3VjNRMlBJMStQZmI1SnFpT28vaUFKRXFRem0vYVlMOVRX"
    "aHd2RmZnQ3hubXNzL1E2TVVNU0d6WlBWSExiVG92MG9UcTRQQkYwYzR4eS9uQXJCYmU3M2ww"
    "VHZmU2Vrd1JnMVpqMzFqcmxlRXpGR1BnaVM3R1pjRExEQ0dVNGJSREUvcTJ6SnRFVi9SNXA2"
    "UmdKaU9XNlFlbklHYTZIOHNxdjJmLzQ1RDRVWjFtbnl0RTY4bm9hMUR1K3NqM2tNUitnZG1S"
    "K2IzN3c1aDgxbmpNYnNsc1FRejhVeVpEenVjWnlxSWdYaFBTcTVjTzlwbkZ5Y3lxWjAwOUtG"
    "Wis2MHNDNmRjVzJ3Skh0V3VDWTZUSTVvT1FSZUNCS3gxUHEzT2lSSTBESXZQcmVFck4xVE03"
    "L1hqQTBPdkxERkNUcVl4cmd2YkNnYllZMVl4MmdTdlp5Vi82N3QyejVmM1FCZmwyelhrS2tE"
    "K2piN3VaR2lFY09UYUs4WjdaTDlWNklLZXphejJOQVRJb3dVazhLdTBlTk5zMjNCZk5OMFVK"
    "bldGT1duRnRqbzl5Q0pqdHR3ZVN3R3ZpRE5pY0VLdFNPRGozTHZ3T1hlaU5qU0VHMnZFRTFn"
    "V09pZVF1emxNTFlJZU5MdHlVN3NPempXMVo3RWhSV3NkRVJIS3dwSm9zVWtzUDYvQW9yeWdm"
    "cHcxSzNlMFhrV0taM3l0SHdRZGVpd2ZxSTdrd0FqeExYSDgzbHp1QlRhWmtNTFRxcStLMGlM"
    "M3R4aFovdy9jZkp2NGdqV2xBM3VFbkt0VkNpcGljcG1rUVRKbitYN0xHN3Nvd0JHaFRabEVO"
    "QTlpMXM4aUpOUE5Ka3BPaGxuaWZmQzR6Njl3SmxBPTwvY2VydD4KICAgIDxjZXJ0Pk1JSURP"
    "akNDQVNLZ0F3SUJBZ0lRVFlaYVozZkExY0FhK1F5S0lwaDVnakFOQmdrcWhraUc5dzBCQVFz"
    "RkFEQTVNVGN3TlFZRFZRUURFeTVIYjI5bmJHVWdRMnh2ZFdRZ1MyVjVJRlpoZFd4MElGTmxj"
    "blpwWTJVZ1NXNTBaWEp0WldScFlYUmxJRU5CTUI0WERUSXpNRFV5TXpJeE5EWXhNbG9YRFRJ"
    "MU1EUXdPVEF3TURBd01Gb3dNakV3TUM0R0ExVUVBeE1uUjI5dloyeGxJRU5zYjNWa0lFdGxl"
    "U0JXWVhWc2RDQlRaWEoyYVdObElFVnVaSEJ2YVc1ME1Ga3dFd1lIS29aSXpqMENBUVlJS29a"
    "SXpqMERBUWNEUWdBRVhvMlByVCt1aHZmSmkzcUtLVDdZUENZOCs3MEVxNHQzY2pjemptdnl2"
    "aFU4SFZyUHJlVVl6amt6ZGd6SDRucnNUU0wwemJoYitCZm11Zy83SFlua2thTVFNQTR3REFZ"
    "RFZSMFRBUUgvQkFJd0FEQU5CZ2txaGtpRzl3MEJBUXNGQUFPQ0FnRUFlK1kyWng3OC9QMVpQ"
    "MTR3dGJ5d3ZRdTFjRWh4Z05JYTlEaUlnck1IVzFEaDJJVldoZklzOXZOc0dkdXJOaENSZU0y"
    "d2l5YWFoWU1MZFNaWWNCMW9Ba0RQL2loT3VMT0pMS3RBZnhneE9Zb0J3cUhlNEp6VTZBaGlw"
    "SHRDZy9vTm9nZlBlODJDVHhCZGluaCtONGpRQzU1emZTclBISGFERHFieW92ZWxuSlJGVkE5"
    "Z1BLVzYwSFFpZitzekZmZG9oZEFVa3RkaWRKNTRWUU9xNDFFcUlLU2UyeVFZam5oN2RrNGgz"
    "SWNnaXQxbEdKZVFxMU1CZkUxaDBRNisrM1M4VVNONmo3Y0JwRUtTQUdYb1IyS3MwVnBhM0RY"
    "Q3U2OVdOb0RGeEZwY1FWd1IxcjJqZWw3QVliaFc2eFMzLzFUMDBBMU8xSFRUVjg0MjVOWllp"
    "Z0doaTU2cnhkS3NPSFdYTnhQd3F6NEVqYld2dEs1akp1bEVSZk5tQXFSYW41TTF2Qytka2xN"
    "NWo2UkZoM3U4ZTZTVHNqK00zQ0NpTkQrTGJDNXZZdHJVaEwvZmpJRytERHBZdVEwMU1rYjFv"
    "dU9rYnhrbXU3RVdIWDFiNGN6TGJRdkFCa0xFOWtUaFVON2xqSkZxeG4zU1FJOEsyVVZtUmEv"
    "QXRrM3ZmK0pCcFllOWhITElaWnlGMEE5RlF5SE1NN0xrL2tRaDBwR3pNcXViWkxyNll1alVs"
    "VTdabS9PelJIQ3k0WS91Z1NOSVUvb2pjbEpHQUp2RWovRmw4YXpTNXZUZW83VUdzNUtaNnBV"
    "QS9MdGZoWGI4emdjbFliNXlDRmJMY3VQNVAzTzlZYlRLdVdXcjlZa3lTQkZpczRpSkFoVmxG"
    "MVhHNGdCajZBVDJIdkxZcjZjUWdZcz08L2NlcnQ+CiAgPC9lbmRwb2ludHM+CjwvY2VydGlm"
    "aWNhdGU+Cg==";

// A valid cert xml from https://www.gstatic.com/cryptauthvault/v0/cert.sig.xml
constexpr char kSigXmlB64[] =
    "PD94bWwKdmVyc2lvbj0iMS4wIiBlbmNvZGluZz0iVVRGLTgiPz4KPHNpZ25hdHVyZT4KICAg"
    "IDxpbnRlcm1lZGlhdGVzPgogICAgICAgIDxjZXJ0Pk1JSUZHakNDQXdLZ0F3SUJBZ0lRSGZs"
    "bkROV2tqMnl4ZUQxSUI2R2RUVEFOQmdrcWhraUc5dzBCQVFzRkFEQXhNUzh3TFFZRFZRUURF"
    "eVpIYjI5bmJHVWdRMnh2ZFdRZ1MyVjVJRlpoZFd4MElGTmxjblpwWTJVZ1VtOXZkQ0JEUVRB"
    "ZUZ3MHhPREExTURjeE9EVTRNVEJhRncweU9EQTFNRGd4T0RVNE1UQmFNRGt4TnpBMUJnTlZC"
    "QU1UTGtkdmIyZHNaU0JEYkc5MVpDQkxaWGtnVm1GMWJIUWdVMlZ5ZG1salpTQkpiblJsY20x"
    "bFpHbGhkR1VnUTBFd2dnSWlNQTBHQ1NxR1NJYjNEUUVCQVFVQUE0SUNEd0F3Z2dJS0FvSUNB"
    "UUR2ZE91OGZlUHJNU0thbHh6ZmEzSGFLNmxiQi9MTjBUbVhnU056a1FGalBIUnBrZnB2OG9P"
    "NjYzQzZ5MlRRaWpLT1ViSkl3SFAvYlZpQlo2TWliNmQyNENXbVR0WmptUmdlblJkYTNOMmJG"
    "bVNlWlpPcTZBOTQxbDZJb1NPZ09hbklodGpvMzUvWGVGRFpGV0g1WU5NU2pCRCtMVGhZRXZ2"
    "bHlHeUdVZ2I3Y0RqYURsQ3ZzTmtRcDFQMmdsQ3FTZTNPUFJ3YklubGZ3SENONU9JbXU5aVRZ"
    "cWYvVG5ubjhxbW5ya2JkRi9iMVU4OVRlTHJ1UzJFQlBkSnE4c0drZlRkTGhabjVDVitzQ0la"
    "RE9MSjIzUDIzdVpLVFhGNStOckhsbFlDNGtCOEpzcDhqZFZZQnhwU3ZnL25zSE9ZTnhLTFNP"
    "Q3pWb3plM1lHczk4QXhIRmEvZm5JTm1lc0w0ZUtuT1NQSU1ZRjhlWVdDaUNYS0Z6MTc4cVBl"
    "Wks2dzdSR3JYQ0M1VmNxemFFcGRHaG9ZbTRlMTdkZ2pCTUxXa3ZPZXEraVdFUFdjK2hkL3lz"
    "ek9BZEhPVnowZnlKZXlBSFdudjN2bWVIbFlJK3N3TmZaNENaaDFyVEZNeHh4TjRITWd6YWlI"
    "RkN6R1dIbXV5TmRVMVRlS0Z0YVFlcTdNaUxBVndDMlYwL2QweVN5UFIyZjhPZGdQS2lTTVNC"
    "VC9QWWpiWG9UM2ZIMkdZcVFTVG8rbnVqeTMvUHFYQXhQMFVraTdPbnMxRVNkWUpLTDU3bXpZ"
    "VHNVT1BwTHZGamJNUWhRZXViQzY0bHpTUE1tZkIxbjdTYktLZHBPWUVPOEZkSGx4VFZ5WVFC"
    "bWlGNElDaEcvMW4vcWdCSnpNc1hRTGk2NVY4RGtaZHdDYlhod0lEQVFBQm95WXdKREFPQmdO"
    "VkhROEJBZjhFQkFNQ0FZWXdFZ1lEVlIwVEFRSC9CQWd3QmdFQi93SUJBVEFOQmdrcWhraUc5"
    "dzBCQVFzRkFBT0NBZ0VBUStHM3YzSkNiekNoQnM4SFVHeDZpMlRNbTFOWk03MStjaGJBMkpG"
    "OURlOGtWZC9yMkNFVHZ2QlJMWGNUUGNXV0EwK1BSREdhRG1pNFRSM2JKaFhnQlN0ZWNRWmtR"
    "dHpJM1pjZEZmSTByVE5lQ2V2ZkhwNW5KanRCK0FZb21DVEtOcmxOTHBrOVliSm9zcUVLVkxR"
    "QmhsTE5ZbTNQVDRDUVlKMU51YkxMdEtGMWNuNForZWF5eHVkMWtEclpXRnlONUNZZXdPcnRY"
    "YzhvQ3luajhIMC9OeWRPdUNSUVUyYy9VWFdtdnNtbFJSZmZISkVYTHFDTWl0VEhWOXc0VkhF"
    "Vmc5WVlzc3huby9qV3RwK2I0ejhKc0UydmtKanMydG1PdmZpTXVwYkp4OWg2emoyajA0cmpo"
    "Zi9BK3ZHUFJLT0Q1V3RiYlg0QW4yK3N6c05MbUVSQmZXVU5zTzFBYVNUYzNXK0FKT2pyRzMw"
    "dGV3UzdqRlJQbHVUdGdCK2ttb3pTVzBNVS9CZ0FZSnVOS1JWUDh6a2xWbVFxSlJicnJ4U3py"
    "dkh6Smx6L2x2RnU5TUQ3bkd0aUZxVDlWZ2dGanFxNXZnbjVzckJwM0RxNEdER2VyZytIQ0RD"
    "TjlxZ25MMWdCY0t6Q01LMW9UMGJDUldaR2NrVDI4V01uZmNnWi9mdUVWTmdRY0VYTGdXaVpX"
    "WkRCRVZsTWg3dTJRb09yMkxYd1h1WE1FOGs4N3JBUWJ4dkdMaHl4cTJ1TnhVZEgxNnVsam03"
    "cDV1MlFvYnlxeHFmMnJPR0pZQ0JMSzJKUDc0ZDZObDZoRDVGR0JCYU82bU4wT2puL1NoSjFD"
    "cTlvM3dDSG9MWW41NXdKblhZdTdRWEFYNjIzMGg3ZWtYcGJ4UFBITzR4MFZhcjVwKzg9PC9j"
    "ZXJ0PgogICAgICAgIDxjZXJ0Pk1JSUZDakNDQXZLZ0F3SUJBZ0lSQU43ZDFJbk9qV0dUVVQ1"
    "NTh6V1BMd0V3RFFZSktvWklodmNOQVFFTEJRQXdJREVlTUJ3R0ExVUVBeE1WUjI5dloyeGxJ"
    "RU55ZVhCMFFYVjBhRlpoZFd4ME1CNFhEVEU0TURVd09UQXhNakF3TmxvWERUSTRNRFV4TURB"
    "eE1qQXdObG93T1RFM01EVUdBMVVFQXhNdVIyOXZaMnhsSUVOc2IzVmtJRXRsZVNCV1lYVnNk"
    "Q0JUWlhKMmFXTmxJRWx1ZEdWeWJXVmthV0YwWlNCRFFUQ0NBaUl3RFFZSktvWklodmNOQVFF"
    "QkJRQURnZ0lQQURDQ0Fnb0NnZ0lCQU85MDY3eDk0K3N4SXBxWEhOOXJjZG9ycVZzSDhzM1JP"
    "WmVCSTNPUkFXTThkR21SK20veWc3cnJjTHJMWk5DS01vNVJza2pBYy85dFdJRm5veUp2cDNi"
    "Z0phWk8xbU9aR0I2ZEYxcmMzWnNXWko1bGs2cm9EM2pXWG9paEk2QTVxY2lHMk9qZm45ZDRV"
    "TmtWWWZsZzB4S01FUDR0T0ZnUysrWEliSVpTQnZ0d09Ob09VSyt3MlJDblUvYUNVS3BKN2M0"
    "OUhCc2llVi9BY0kzazRpYTcySk5pcC85T2VlZnlxYWV1UnQwWDl2VlR6MU40dXU1TFlRRTkw"
    "bXJ5d2FSOU4wdUZtZmtKWDZ3SWhrTTRzbmJjL2JlNWtwTmNYbjQyc2VXVmdMaVFId215bnlO"
    "MVZnSEdsSytEK2V3YzVnM0VvdEk0TE5Xak43ZGdhejN3REVjVnI5K2NnMlo2d3ZoNHFjNUk4"
    "Z3hnWHg1aFlLSUpjb1hQWHZ5bzk1a3JyRHRFYXRjSUxsVnlyTm9TbDBhR2hpYmg3WHQyQ01F"
    "d3RhUzg1NnI2SllROVp6NkYzL0t6TTRCMGM1WFBSL0lsN0lBZGFlL2UrWjRlVmdqNnpBMTlu"
    "Z0ptSFd0TVV6SEhFM2djeUROcUljVUxNWlllYTdJMTFUVk40b1cxcEI2cnN5SXNCWEFMWlhU"
    "OTNUSkxJOUhaL3c1MkE4cUpJeElGUDg5aU50ZWhQZDhmWVppcEJKT2o2ZTZQTGY4K3BjREUv"
    "UlNTTHM2ZXpVUkoxZ2tvdm51Yk5oT3hRNCtrdThXTnN4Q0ZCNjVzTHJpWE5JOHlaOEhXZnRK"
    "c29wMms1Z1E3d1YwZVhGTlhKaEFHYUlYZ2dLRWIvV2YrcUFFbk15eGRBdUxybFh3T1JsM0FK"
    "dGVIQWdNQkFBR2pKakFrTUE0R0ExVWREd0VCL3dRRUF3SUJoakFTQmdOVkhSTUJBZjhFQ0RB"
    "R0FRSC9BZ0VCTUEwR0NTcUdTSWIzRFFFQkN3VUFBNElDQVFCbGJXY1hnRDRLQ0JnQnBOVTZ6"
    "ODY3NW9BaUpiNFl3ckk4R1QyWTVsZ2x6NmprbXk5Z1BaZFU1NlBQeVhPME1JQkNzbW1YeEVj"
    "VlVSRFVMdVg4REpzYnp1cW5iTThtRWJtSzhDVmxNaHE5Tk5PRlpNQ3RuaHU2NDdsWStaYWJC"
    "VVlyNGJTZ1BpSnh3d01vcjNjMTVQRngvZGVaQVllQXRiVjl6VzBRMDd5WG1qT29RaHRndkpq"
    "RU85cHd4d2YxZ2t0RDlXYmo3T3BTaUxObEtHcExGT1RqbTBja3pJQkdnd3ZZV3ArQTZMQ2pt"
    "T3p1VjkxaGRVRjRMRXJHMFo2R1FWbGxhekhTSjVvYU5FSng2d3lKbnQrZ0w0VERYd2dERjdR"
    "cGtTaXhCZ2Z4NVRZOVFWc1RpL3dMemtEQ2psOHh1WDNZWGRsb2pvZmtzeGE4M01BRjZXOFB1"
    "YTRaaEtGVGNuR0FGUU1UZlBNVXQwQkFFa3lUeGxBb3ZaN0grWlhDa0Q0N1RrY0dJOUtXYXY3"
    "ZERMOVA0SXFRbGpEOWZyL1IwYW5sSCtyd0puOWpKMVVxVGJXb0hnWXI4cU5hNFNrRDNXZlpo"
    "YjdUUUpiVUQ2Vm9jckVxQno2UDlXZ0pGbEIwTm41NHVlN1JsRkM1K25sVjhtNlpQYmY2K2Y3"
    "d1ZPclZuME9ieHEydDlSU2lMOUFlYlBEZ2Z0cytKZ3ZmbG1QU09IRDVXKzRvNDJTNC9odWVs"
    "ZkZ4dUlNMWFpZDhsWmlwMFRKQnpZWFdtT0NwMlNQSGROMHdJcDcvbTFGako1WjdyanFuMGRC"
    "K29YdkhhcHl3QWR5bUVhVm0vcnM5NDBkNTBjR2cvMVJmdkFDM29ZU3laZTk5WWVLOURFUW8x"
    "MjQ5KzBuNlFoaG9KUUpBQ3c9PTwvY2VydD4KICAgIDwvaW50ZXJtZWRpYXRlcz4KICAgIDxj"
    "ZXJ0aWZpY2F0ZT5NSUlGR0RDQ0F3Q2dBd0lCQWdJUUZ5UEdEamt2bm9XSGJxcld6MndnQ0RB"
    "TkJna3Foa2lHOXcwQkFRc0ZBREE1TVRjd05RWURWUVFERXk1SGIyOW5iR1VnUTJ4dmRXUWdT"
    "MlY1SUZaaGRXeDBJRk5sY25acFkyVWdTVzUwWlhKdFpXUnBZWFJsSUVOQk1CNFhEVEl6TURV"
    "eU16SXhORFl4TjFvWERUSTRNRFV5TkRJeE5EWXhOMW93TlRFek1ERUdBMVVFQXhNcVIyOXZa"
    "MnhsSUVOc2IzVmtJRXRsZVNCV1lYVnNkQ0JUWlhKMmFXTmxJRk5wWjI1cGJtY2dTMlY1TUlJ"
    "Q0lqQU5CZ2txaGtpRzl3MEJBUUVGQUFPQ0FnOEFNSUlDQ2dLQ0FnRUF6eFVhR01TNkhXTlor"
    "RytwVkIzQVAxUlZwQ3RhMUtINm1HZi9YQ0ZOL3hMcU9mN1JsVDY4OXNibUFFRS8zQVhFNUVS"
    "bjlHY0ZoZHJDL3lBeTBhVmt5L0RGclNvR1BZNmdLN2ZhaDVJZW1oZVdVOTNFaGtZTHZWMWRo"
    "eDJ5Tm9TY2NYRTVXVHdCTzJiVk5ZOFB4MGR6S0RYelJhK2pVZ3FZd2xSZHBtcGp2dDVoNFE2"
    "N3BrOUZOUXFsb2E5WWxTaVN2cjJRR3VXcHRnaUV2TnloTEg5RGZrWnVPVTU2T05XTTV2UmFV"
    "Ti9rbjFMOElIcDdOVG9VeFdVWFJBb0hwNTRGSFk3cEpicEU5S1RYNTk3MWlYY05BWHp3akhQ"
    "Vk0wSUZGZUx1Q25oaXJlSVRRdmdvRXBzMUY0S3B1R1U0emhzeUtZdUt0RFhGd2tnc2YxU2tl"
    "Z3RnSzJpL2ZGUTV5VDdGOEkwVVlmL2VWVENOSy9WaGd3RTQzdUp4WEdtMklpV1BUVHhNelB6"
    "OVlOU1NVNHhMN0xHeXZaN1FJQzZoRndyYU9TVTlWelZHM0YzQnZNdU5BR2prY0duUjVCa203"
    "b3ZqL0lGcHR2bjdKbVE0QTlMR01yQXllUHJ4VnFQK2tQYVdIc01RZ2NxbVUxMTQzK1pSQ0p4"
    "U0xNYXhOSmVtclpyYWpsdGgzamQ3WVk4bVozanJsSHBDdHlJQUhKUG5obFV6ejN1YktuMTJZ"
    "ZTFHWHp1YUVoWnpKR1dVSmN4b3dOSVE5Q1RvRWtTZWFOdXVDNkcrOGZXTEl2bkZCQSs0OWlR"
    "RW5HVTBrWGFRaXk1aTVDNXB4ejVhNzJFMktvakhzdkhCdTFNeGRXdFJUTWtYUWpJcnpCZE15"
    "VkpCUEwvVER2MjBXeFQ3Ty9oL3lpOENBd0VBQWFNZ01CNHdEZ1lEVlIwUEFRSC9CQVFEQWdl"
    "QU1Bd0dBMVVkRXdFQi93UUNNQUF3RFFZSktvWklodmNOQVFFTEJRQURnZ0lCQUR2RC8weDA0"
    "bEtiYUxka2dlbGxVYjlFL3dnNWdYdnRicGZIb0pNRGpla1JUWDEvMUNUL1pxU0VLV3NpVFI4"
    "OGtZMFB6WHhHcGRxZGZKbUxUR3hmcUxDR1puQVArckxYNng5TVNCNlRWSS9IZVVHOWJlZkZU"
    "Y3VDWXhWNGQ0K0hDTlZQZjZiOGhLTVNvOTVFWnhLb0gxUmZYUWxDZS9mMU8zRWYzS1VLN25H"
    "NHhsY3RtTktJZC9mRUFndU1oVUE4MytlUmZEdkhFWmJ5RVdMMWk3dnY5R3NEZWNOM2hZSHRD"
    "UTl0a29IRlJEOEg3T2w4VWRKaGR4dXJ5NUdyS3RZZE5FOWllRm9iMHZvWmR5bTNNMEJPSmZK"
    "eUhhWmJQWVVzWWgzVlgzZm1xbzV5S3Q5VHdDVkhZWDY5dUF0VXVQcW4vSktVZ2hGMysrbUI5"
    "dHd2anNwckhGTWdUM215QkVRbFgzNXZKcU5CdVBPUnF4eU5zWitQQnRlNnk3cGxVTk9PRnBU"
    "VGJpdnlWUk9pSHN4cXcvUWtDV1hqMW9ESDh6bVNqUTFvaDlObUgyRm9nMG9GQ0YyUGFCem9M"
    "UGFqRmFPTTRYb3hTalBYVXF3QW04WFJFYi8va0ljeDZXZzNGb1BoZHlLQzVESnJ1cmd6ZEQv"
    "WkdQZUVJYzl1RmkzYUNsRVlieVp2cUpWUGgwS2k1TldJcGJoTVVCTWx2VllrWnJOc2o4dUtI"
    "aVFxekxiOFBsN2xkcFZWSUtiVnV2cjh4bzZBZVpaWWI4MXpIWk9IMno4dEtjNDdpbTZUTGFY"
    "SlJJL2xhcm8rN3FRMDlRNzV4cGdobW9LTWNPK3EydUIwS3ZWSmY5eXI1V0tFN2RUYU5aVDZj"
    "Wmp5N0VMaFlhbjZHaEQ3U3RwanJtcUR3dy9PPC9jZXJ0aWZpY2F0ZT4KICAgIDx2YWx1ZT5w"
    "Y0RxTDZiZXRuMXFhN1p6dDB5ZmpMTWFRZ2lKM1JpM0UyNkR2eHlBU1l1MVNGSnNZT2txbVlD"
    "ekE3Zy9meEthNWZTTlY3MzNLNnI1dnZRaEVtYTlseUY3ZlVBMllWYm9JRFlnczgxeWtNNUJn"
    "YjFDZmJPWEFRRk4zLzk3YkJQZjgzaFpqMzhkYkNIOGVHY3p0ZDMzNzdFYzFITmVHMXg5ZVhw"
    "WGFJU256R1Rud0FuMFFvTmJ2UXZ6MlFPYUZWOHRnUE1KRlFPREJWaGtFWVpPamlydFZpMVNV"
    "M1hNeFFBZkdOM0ozaEtsUTB6L3JVS3BkSDhnNG1Tb1k3N2xZdlFWRklIVHBWMXdMelV4M0lv"
    "QzZqT1lEQURMVlFTL2QzMy9zRjJnTS9IMVNZb1pEN0hPVUhWUVJVK3pvZzQvVkI2TzdIREpk"
    "dm41WkxNUkxRcWNWUlg4R1o4SkNBaW9GeVRJZmdCcjk4OTk4QW92V0o1Lzl4NmNRd05DWFBM"
    "MnFIMVRjVHJWV2xrZmdBTHhiUlFTM0RpQ1lwandmSzl5RmNMb2NUMkU5ekEycytRZ292L1J3"
    "MW9OSkhKU1N3Z2p5RXc2UEdYdjIwSDAyblg3b2dtUnNZVEg4amEwN29mbmcxd2swMm1uL1Ny"
    "dzg5eVplT0Q5bU9pTGVoNU83Mk5YbHFtYkZuS0JmVm83Q3pQaW43bXltZjI0Z1R2U0JxNDcy"
    "S1NkejJCRUlGTE1QY1BFVGJzZ2NsUEcrM0NLbzVIVmRWeTJOUzNBY05GTmhpWTZzWERiMkl0"
    "N2ZULzk4a2ZMYW9WWWJOOVhvTnZaelpGYnJoU3hZK2dZaXNCamZHdSsrUUl2bGZjK0FveWpY"
    "Y1VKYTVsL25HQlgxeGxVaG9xeVRsRzN5V3krUXEyWnUxdz08L3ZhbHVlPgo8L3NpZ25hdHVy"
    "ZT4=";

// Intermediate cert is set to an endpoint cert.
constexpr char kInvalidSigXmlB64[] =
    "PD94bWwgdmVyc2lvbj0iMS4wIiBlbmNvZGluZz0iVVRGLTgiPz4KPHNpZ25hdHVyZT4KICA8"
    "aW50ZXJtZWRpYXRlcz4KICAgIDxjZXJ0Pk1JSUZHRENDQXdDZ0F3SUJBZ0lRRnlQR0Rqa3Zu"
    "b1dIYnFyV3oyd2dDREFOQmdrcWhraUc5dzBCQVFzRkFEQTVNVGN3TlFZRFZRUURFeTVIYjI5"
    "bmJHVWdRMnh2ZFdRZ1MyVjVJRlpoZFd4MElGTmxjblpwWTJVZ1NXNTBaWEp0WldScFlYUmxJ"
    "RU5CTUI0WERUSXpNRFV5TXpJeE5EWXhOMW9YRFRJNE1EVXlOREl4TkRZeE4xb3dOVEV6TURF"
    "R0ExVUVBeE1xUjI5dloyeGxJRU5zYjNWa0lFdGxlU0JXWVhWc2RDQlRaWEoyYVdObElGTnBa"
    "MjVwYm1jZ1MyVjVNSUlDSWpBTkJna3Foa2lHOXcwQkFRRUZBQU9DQWc4QU1JSUNDZ0tDQWdF"
    "QXp4VWFHTVM2SFdOWitHK3BWQjNBUDFSVnBDdGExS0g2bUdmL1hDRk4veExxT2Y3UmxUNjg5"
    "c2JtQUVFLzNBWEU1RVJuOUdjRmhkckMveUF5MGFWa3kvREZyU29HUFk2Z0s3ZmFoNUllbWhl"
    "V1U5M0Voa1lMdlYxZGh4MnlOb1NjY1hFNVdUd0JPMmJWTlk4UHgwZHpLRFh6UmEralVncVl3"
    "bFJkcG1wanZ0NWg0UTY3cGs5Rk5RcWxvYTlZbFNpU3ZyMlFHdVdwdGdpRXZOeWhMSDlEZmta"
    "dU9VNTZPTldNNXZSYVVOL2tuMUw4SUhwN05Ub1V4V1VYUkFvSHA1NEZIWTdwSmJwRTlLVFg1"
    "OTcxaVhjTkFYendqSFBWTTBJRkZlTHVDbmhpcmVJVFF2Z29FcHMxRjRLcHVHVTR6aHN5S1l1"
    "S3REWEZ3a2dzZjFTa2VndGdLMmkvZkZRNXlUN0Y4STBVWWYvZVZUQ05LL1ZoZ3dFNDN1SnhY"
    "R20ySWlXUFRUeE16UHo5WU5TU1U0eEw3TEd5dlo3UUlDNmhGd3JhT1NVOVZ6VkczRjNCdk11"
    "TkFHamtjR25SNUJrbTdvdmovSUZwdHZuN0ptUTRBOUxHTXJBeWVQcnhWcVAra1BhV0hzTVFn"
    "Y3FtVTExNDMrWlJDSnhTTE1heE5KZW1yWnJhamx0aDNqZDdZWThtWjNqcmxIcEN0eUlBSEpQ"
    "bmhsVXp6M3ViS24xMlllMUdYenVhRWhaekpHV1VKY3hvd05JUTlDVG9Fa1NlYU51dUM2Rys4"
    "ZldMSXZuRkJBKzQ5aVFFbkdVMGtYYVFpeTVpNUM1cHh6NWE3MkUyS29qSHN2SEJ1MU14ZFd0"
    "UlRNa1hRaklyekJkTXlWSkJQTC9URHYyMFd4VDdPL2gveWk4Q0F3RUFBYU1nTUI0d0RnWURW"
    "UjBQQVFIL0JBUURBZ2VBTUF3R0ExVWRFd0VCL3dRQ01BQXdEUVlKS29aSWh2Y05BUUVMQlFB"
    "RGdnSUJBRHZELzB4MDRsS2JhTGRrZ2VsbFViOUUvd2c1Z1h2dGJwZkhvSk1EamVrUlRYMS8x"
    "Q1QvWnFTRUtXc2lUUjg4a1kwUHpYeEdwZHFkZkptTFRHeGZxTENHWm5BUCtyTFg2eDlNU0I2"
    "VFZJL0hlVUc5YmVmRlRjdUNZeFY0ZDQrSENOVlBmNmI4aEtNU285NUVaeEtvSDFSZlhRbENl"
    "L2YxTzNFZjNLVUs3bkc0eGxjdG1OS0lkL2ZFQWd1TWhVQTgzK2VSZkR2SEVaYnlFV0wxaTd2"
    "djlHc0RlY04zaFlIdENROXRrb0hGUkQ4SDdPbDhVZEpoZHh1cnk1R3JLdFlkTkU5aWVGb2Iw"
    "dm9aZHltM00wQk9KZkp5SGFaYlBZVXNZaDNWWDNmbXFvNXlLdDlUd0NWSFlYNjl1QXRVdVBx"
    "bi9KS1VnaEYzKyttQjl0d3Zqc3BySEZNZ1QzbXlCRVFsWDM1dkpxTkJ1UE9ScXh5TnNaK1BC"
    "dGU2eTdwbFVOT09GcFRUYml2eVZST2lIc3hxdy9Ra0NXWGoxb0RIOHptU2pRMW9oOU5tSDJG"
    "b2cwb0ZDRjJQYUJ6b0xQYWpGYU9NNFhveFNqUFhVcXdBbThYUkViLy9rSWN4NldnM0ZvUGhk"
    "eUtDNURKcnVyZ3pkRC9aR1BlRUljOXVGaTNhQ2xFWWJ5WnZxSlZQaDBLaTVOV0lwYmhNVUJN"
    "bHZWWWtack5zajh1S0hpUXF6TGI4UGw3bGRwVlZJS2JWdXZyOHhvNkFlWlpZYjgxekhaT0gy"
    "ejh0S2M0N2ltNlRMYVhKUkkvbGFybys3cVEwOVE3NXhwZ2htb0tNY08rcTJ1QjBLdlZKZjl5"
    "cjVXS0U3ZFRhTlpUNmNaank3RUxoWWFuNkdoRDdTdHBqcm1xRHd3L088L2NlcnQ+CiAgPC9p"
    "bnRlcm1lZGlhdGVzPgogIDxjZXJ0aWZpY2F0ZT5NSUlGR0RDQ0F3Q2dBd0lCQWdJUUZ5UEdE"
    "amt2bm9XSGJxcld6MndnQ0RBTkJna3Foa2lHOXcwQkFRc0ZBREE1TVRjd05RWURWUVFERXk1"
    "SGIyOW5iR1VnUTJ4dmRXUWdTMlY1SUZaaGRXeDBJRk5sY25acFkyVWdTVzUwWlhKdFpXUnBZ"
    "WFJsSUVOQk1CNFhEVEl6TURVeU16SXhORFl4TjFvWERUSTRNRFV5TkRJeE5EWXhOMW93TlRF"
    "ek1ERUdBMVVFQXhNcVIyOXZaMnhsSUVOc2IzVmtJRXRsZVNCV1lYVnNkQ0JUWlhKMmFXTmxJ"
    "Rk5wWjI1cGJtY2dTMlY1TUlJQ0lqQU5CZ2txaGtpRzl3MEJBUUVGQUFPQ0FnOEFNSUlDQ2dL"
    "Q0FnRUF6eFVhR01TNkhXTlorRytwVkIzQVAxUlZwQ3RhMUtINm1HZi9YQ0ZOL3hMcU9mN1Js"
    "VDY4OXNibUFFRS8zQVhFNUVSbjlHY0ZoZHJDL3lBeTBhVmt5L0RGclNvR1BZNmdLN2ZhaDVJ"
    "ZW1oZVdVOTNFaGtZTHZWMWRoeDJ5Tm9TY2NYRTVXVHdCTzJiVk5ZOFB4MGR6S0RYelJhK2pV"
    "Z3FZd2xSZHBtcGp2dDVoNFE2N3BrOUZOUXFsb2E5WWxTaVN2cjJRR3VXcHRnaUV2TnloTEg5"
    "RGZrWnVPVTU2T05XTTV2UmFVTi9rbjFMOElIcDdOVG9VeFdVWFJBb0hwNTRGSFk3cEpicEU5"
    "S1RYNTk3MWlYY05BWHp3akhQVk0wSUZGZUx1Q25oaXJlSVRRdmdvRXBzMUY0S3B1R1U0emhz"
    "eUtZdUt0RFhGd2tnc2YxU2tlZ3RnSzJpL2ZGUTV5VDdGOEkwVVlmL2VWVENOSy9WaGd3RTQz"
    "dUp4WEdtMklpV1BUVHhNelB6OVlOU1NVNHhMN0xHeXZaN1FJQzZoRndyYU9TVTlWelZHM0Yz"
    "QnZNdU5BR2prY0duUjVCa203b3ZqL0lGcHR2bjdKbVE0QTlMR01yQXllUHJ4VnFQK2tQYVdI"
    "c01RZ2NxbVUxMTQzK1pSQ0p4U0xNYXhOSmVtclpyYWpsdGgzamQ3WVk4bVozanJsSHBDdHlJ"
    "QUhKUG5obFV6ejN1YktuMTJZZTFHWHp1YUVoWnpKR1dVSmN4b3dOSVE5Q1RvRWtTZWFOdXVD"
    "NkcrOGZXTEl2bkZCQSs0OWlRRW5HVTBrWGFRaXk1aTVDNXB4ejVhNzJFMktvakhzdkhCdTFN"
    "eGRXdFJUTWtYUWpJcnpCZE15VkpCUEwvVER2MjBXeFQ3Ty9oL3lpOENBd0VBQWFNZ01CNHdE"
    "Z1lEVlIwUEFRSC9CQVFEQWdlQU1Bd0dBMVVkRXdFQi93UUNNQUF3RFFZSktvWklodmNOQVFF"
    "TEJRQURnZ0lCQUR2RC8weDA0bEtiYUxka2dlbGxVYjlFL3dnNWdYdnRicGZIb0pNRGpla1JU"
    "WDEvMUNUL1pxU0VLV3NpVFI4OGtZMFB6WHhHcGRxZGZKbUxUR3hmcUxDR1puQVArckxYNng5"
    "TVNCNlRWSS9IZVVHOWJlZkZUY3VDWXhWNGQ0K0hDTlZQZjZiOGhLTVNvOTVFWnhLb0gxUmZY"
    "UWxDZS9mMU8zRWYzS1VLN25HNHhsY3RtTktJZC9mRUFndU1oVUE4MytlUmZEdkhFWmJ5RVdM"
    "MWk3dnY5R3NEZWNOM2hZSHRDUTl0a29IRlJEOEg3T2w4VWRKaGR4dXJ5NUdyS3RZZE5FOWll"
    "Rm9iMHZvWmR5bTNNMEJPSmZKeUhhWmJQWVVzWWgzVlgzZm1xbzV5S3Q5VHdDVkhZWDY5dUF0"
    "VXVQcW4vSktVZ2hGMysrbUI5dHd2anNwckhGTWdUM215QkVRbFgzNXZKcU5CdVBPUnF4eU5z"
    "WitQQnRlNnk3cGxVTk9PRnBUVGJpdnlWUk9pSHN4cXcvUWtDV1hqMW9ESDh6bVNqUTFvaDlO"
    "bUgyRm9nMG9GQ0YyUGFCem9MUGFqRmFPTTRYb3hTalBYVXF3QW04WFJFYi8va0ljeDZXZzNG"
    "b1BoZHlLQzVESnJ1cmd6ZEQvWkdQZUVJYzl1RmkzYUNsRVlieVp2cUpWUGgwS2k1TldJcGJo"
    "TVVCTWx2VllrWnJOc2o4dUtIaVFxekxiOFBsN2xkcFZWSUtiVnV2cjh4bzZBZVpaWWI4MXpI"
    "Wk9IMno4dEtjNDdpbTZUTGFYSlJJL2xhcm8rN3FRMDlRNzV4cGdobW9LTWNPK3EydUIwS3ZW"
    "SmY5eXI1V0tFN2RUYU5aVDZjWmp5N0VMaFlhbjZHaEQ3U3RwanJtcUR3dy9PPC9jZXJ0aWZp"
    "Y2F0ZT4KICA8dmFsdWU+cGNEcUw2YmV0bjFxYTdaenQweWZqTE1hUWdpSjNSaTNFMjZEdnh5"
    "QVNZdTFTRkpzWU9rcW1ZQ3pBN2cvZnhLYTVmU05WNzMzSzZyNXZ2UWhFbWE5bHlGN2ZVQTJZ"
    "VmJvSURZZ3M4MXlrTTVCZ2IxQ2ZiT1hBUUZOMy85N2JCUGY4M2haajM4ZGJDSDhlR2N6dGQz"
    "Mzc3RWMxSE5lRzF4OWVYcFhhSVNuekdUbndBbjBRb05idlF2ejJRT2FGVjh0Z1BNSkZRT0RC"
    "VmhrRVlaT2ppcnRWaTFTVTNYTXhRQWZHTjNKM2hLbFEwei9yVUtwZEg4ZzRtU29ZNzdsWXZR"
    "VkZJSFRwVjF3THpVeDNJb0M2ak9ZREFETFZRUy9kMzMvc0YyZ00vSDFTWW9aRDdIT1VIVlFS"
    "VSt6b2c0L1ZCNk83SERKZHZuNVpMTVJMUXFjVlJYOEdaOEpDQWlvRnlUSWZnQnI5ODk5OEFv"
    "dldKNS85eDZjUXdOQ1hQTDJxSDFUY1RyVldsa2ZnQUx4YlJRUzNEaUNZcGp3Zks5eUZjTG9j"
    "VDJFOXpBMnMrUWdvdi9SdzFvTkpISlNTd2dqeUV3NlBHWHYyMEgwMm5YN29nbVJzWVRIOGph"
    "MDdvZm5nMXdrMDJtbi9Tcnc4OXlaZU9EOW1PaUxlaDVPNzJOWGxxbWJGbktCZlZvN0N6UGlu"
    "N215bWYyNGdUdlNCcTQ3MktTZHoyQkVJRkxNUGNQRVRic2djbFBHKzNDS281SFZkVnkyTlMz"
    "QWNORk5oaVk2c1hEYjJJdDdmVC85OGtmTGFvVlliTjlYb052WnpaRmJyaFN4WStnWWlzQmpm"
    "R3UrK1FJdmxmYytBb3lqWGNVSmE1bC9uR0JYMXhsVWhvcXlUbEczeVd5K1FxMlp1MXc9PC92"
    "YWx1ZT4KPC9zaWduYXR1cmU+";

// An old signature xml that wouldn't verify successfully to the kCertXml file.
constexpr char kOldSigXmlB64[] =
    "PD94bWwgdmVyc2lvbj0iMS4wIiBlbmNvZGluZz0iVVRGLTgiPz4KPHNpZ25hdHVyZT4KICA8"
    "aW50ZXJtZWRpYXRlcz4KICAgIDxjZXJ0Pk1JSUZHakNDQXdLZ0F3SUJBZ0lRSGZsbkROV2tq"
    "Mnl4ZUQxSUI2R2RUVEFOQmdrcWhraUc5dzBCQVFzRkFEQXhNUzh3TFFZRFZRUURFeVpIYjI5"
    "bmJHVWdRMnh2ZFdRZ1MyVjVJRlpoZFd4MElGTmxjblpwWTJVZ1VtOXZkQ0JEUVRBZUZ3MHhP"
    "REExTURjeE9EVTRNVEJhRncweU9EQTFNRGd4T0RVNE1UQmFNRGt4TnpBMUJnTlZCQU1UTGtk"
    "dmIyZHNaU0JEYkc5MVpDQkxaWGtnVm1GMWJIUWdVMlZ5ZG1salpTQkpiblJsY20xbFpHbGhk"
    "R1VnUTBFd2dnSWlNQTBHQ1NxR1NJYjNEUUVCQVFVQUE0SUNEd0F3Z2dJS0FvSUNBUUR2ZE91"
    "OGZlUHJNU0thbHh6ZmEzSGFLNmxiQi9MTjBUbVhnU056a1FGalBIUnBrZnB2OG9PNjYzQzZ5"
    "MlRRaWpLT1ViSkl3SFAvYlZpQlo2TWliNmQyNENXbVR0WmptUmdlblJkYTNOMmJGbVNlWlpP"
    "cTZBOTQxbDZJb1NPZ09hbklodGpvMzUvWGVGRFpGV0g1WU5NU2pCRCtMVGhZRXZ2bHlHeUdV"
    "Z2I3Y0RqYURsQ3ZzTmtRcDFQMmdsQ3FTZTNPUFJ3YklubGZ3SENONU9JbXU5aVRZcWYvVG5u"
    "bjhxbW5ya2JkRi9iMVU4OVRlTHJ1UzJFQlBkSnE4c0drZlRkTGhabjVDVitzQ0laRE9MSjIz"
    "UDIzdVpLVFhGNStOckhsbFlDNGtCOEpzcDhqZFZZQnhwU3ZnL25zSE9ZTnhLTFNPQ3pWb3pl"
    "M1lHczk4QXhIRmEvZm5JTm1lc0w0ZUtuT1NQSU1ZRjhlWVdDaUNYS0Z6MTc4cVBlWks2dzdS"
    "R3JYQ0M1VmNxemFFcGRHaG9ZbTRlMTdkZ2pCTUxXa3ZPZXEraVdFUFdjK2hkL3lzek9BZEhP"
    "VnowZnlKZXlBSFdudjN2bWVIbFlJK3N3TmZaNENaaDFyVEZNeHh4TjRITWd6YWlIRkN6R1dI"
    "bXV5TmRVMVRlS0Z0YVFlcTdNaUxBVndDMlYwL2QweVN5UFIyZjhPZGdQS2lTTVNCVC9QWWpi"
    "WG9UM2ZIMkdZcVFTVG8rbnVqeTMvUHFYQXhQMFVraTdPbnMxRVNkWUpLTDU3bXpZVHNVT1Bw"
    "THZGamJNUWhRZXViQzY0bHpTUE1tZkIxbjdTYktLZHBPWUVPOEZkSGx4VFZ5WVFCbWlGNElD"
    "aEcvMW4vcWdCSnpNc1hRTGk2NVY4RGtaZHdDYlhod0lEQVFBQm95WXdKREFPQmdOVkhROEJB"
    "ZjhFQkFNQ0FZWXdFZ1lEVlIwVEFRSC9CQWd3QmdFQi93SUJBVEFOQmdrcWhraUc5dzBCQVFz"
    "RkFBT0NBZ0VBUStHM3YzSkNiekNoQnM4SFVHeDZpMlRNbTFOWk03MStjaGJBMkpGOURlOGtW"
    "ZC9yMkNFVHZ2QlJMWGNUUGNXV0EwK1BSREdhRG1pNFRSM2JKaFhnQlN0ZWNRWmtRdHpJM1pj"
    "ZEZmSTByVE5lQ2V2ZkhwNW5KanRCK0FZb21DVEtOcmxOTHBrOVliSm9zcUVLVkxRQmhsTE5Z"
    "bTNQVDRDUVlKMU51YkxMdEtGMWNuNForZWF5eHVkMWtEclpXRnlONUNZZXdPcnRYYzhvQ3lu"
    "ajhIMC9OeWRPdUNSUVUyYy9VWFdtdnNtbFJSZmZISkVYTHFDTWl0VEhWOXc0VkhFVmc5WVlz"
    "c3huby9qV3RwK2I0ejhKc0UydmtKanMydG1PdmZpTXVwYkp4OWg2emoyajA0cmpoZi9BK3ZH"
    "UFJLT0Q1V3RiYlg0QW4yK3N6c05MbUVSQmZXVU5zTzFBYVNUYzNXK0FKT2pyRzMwdGV3Uzdq"
    "RlJQbHVUdGdCK2ttb3pTVzBNVS9CZ0FZSnVOS1JWUDh6a2xWbVFxSlJicnJ4U3pydkh6Smx6"
    "L2x2RnU5TUQ3bkd0aUZxVDlWZ2dGanFxNXZnbjVzckJwM0RxNEdER2VyZytIQ0RDTjlxZ25M"
    "MWdCY0t6Q01LMW9UMGJDUldaR2NrVDI4V01uZmNnWi9mdUVWTmdRY0VYTGdXaVpXWkRCRVZs"
    "TWg3dTJRb09yMkxYd1h1WE1FOGs4N3JBUWJ4dkdMaHl4cTJ1TnhVZEgxNnVsam03cDV1MlFv"
    "YnlxeHFmMnJPR0pZQ0JMSzJKUDc0ZDZObDZoRDVGR0JCYU82bU4wT2puL1NoSjFDcTlvM3dD"
    "SG9MWW41NXdKblhZdTdRWEFYNjIzMGg3ZWtYcGJ4UFBITzR4MFZhcjVwKzg9PC9jZXJ0Pgog"
    "ICAgPGNlcnQ+TUlJRkNqQ0NBdktnQXdJQkFnSVJBTjdkMUluT2pXR1RVVDU1OHpXUEx3RXdE"
    "UVlKS29aSWh2Y05BUUVMQlFBd0lERWVNQndHQTFVRUF4TVZSMjl2WjJ4bElFTnllWEIwUVhW"
    "MGFGWmhkV3gwTUI0WERURTRNRFV3T1RBeE1qQXdObG9YRFRJNE1EVXhNREF4TWpBd05sb3dP"
    "VEUzTURVR0ExVUVBeE11UjI5dloyeGxJRU5zYjNWa0lFdGxlU0JXWVhWc2RDQlRaWEoyYVdO"
    "bElFbHVkR1Z5YldWa2FXRjBaU0JEUVRDQ0FpSXdEUVlKS29aSWh2Y05BUUVCQlFBRGdnSVBB"
    "RENDQWdvQ2dnSUJBTzkwNjd4OTQrc3hJcHFYSE45cmNkb3JxVnNIOHMzUk9aZUJJM09SQVdN"
    "OGRHbVIrbS95ZzdycmNMckxaTkNLTW81UnNrakFjLzl0V0lGbm95SnZwM2JnSmFaTzFtT1pH"
    "QjZkRjFyYzNac1daSjVsazZyb0QzaldYb2loSTZBNXFjaUcyT2pmbjlkNFVOa1ZZZmxnMHhL"
    "TUVQNHRPRmdTKytYSWJJWlNCdnR3T05vT1VLK3cyUkNuVS9hQ1VLcEo3YzQ5SEJzaWVWL0Fj"
    "STNrNGlhNzJKTmlwLzlPZWVmeXFhZXVSdDBYOXZWVHoxTjR1dTVMWVFFOTBtcnl3YVI5TjB1"
    "Rm1ma0pYNndJaGtNNHNuYmMvYmU1a3BOY1huNDJzZVdWZ0xpUUh3bXlueU4xVmdIR2xLK0Qr"
    "ZXdjNWczRW90STRMTldqTjdkZ2F6M3dERWNWcjkrY2cyWjZ3dmg0cWM1SThneGdYeDVoWUtJ"
    "SmNvWFBYdnlvOTVrcnJEdEVhdGNJTGxWeXJOb1NsMGFHaGliaDdYdDJDTUV3dGFTODU2cjZK"
    "WVE5Wno2RjMvS3pNNEIwYzVYUFIvSWw3SUFkYWUvZStaNGVWZ2o2ekExOW5nSm1IV3RNVXpI"
    "SEUzZ2N5RE5xSWNVTE1aWWVhN0kxMVRWTjRvVzFwQjZyc3lJc0JYQUxaWFQ5M1RKTEk5SFov"
    "dzUyQThxSkl4SUZQODlpTnRlaFBkOGZZWmlwQkpPajZlNlBMZjgrcGNERS9SU1NMczZlelVS"
    "SjFna292bnViTmhPeFE0K2t1OFdOc3hDRkI2NXNMcmlYTkk4eVo4SFdmdEpzb3AyazVnUTd3"
    "VjBlWEZOWEpoQUdhSVhnZ0tFYi9XZitxQUVuTXl4ZEF1THJsWHdPUmwzQUp0ZUhBZ01CQUFH"
    "akpqQWtNQTRHQTFVZER3RUIvd1FFQXdJQmhqQVNCZ05WSFJNQkFmOEVDREFHQVFIL0FnRUJN"
    "QTBHQ1NxR1NJYjNEUUVCQ3dVQUE0SUNBUUJsYldjWGdENEtDQmdCcE5VNno4Njc1b0FpSmI0"
    "WXdySThHVDJZNWxnbHo2amtteTlnUFpkVTU2UFB5WE8wTUlCQ3NtbVh4RWNWVVJEVUx1WDhE"
    "SnNienVxbmJNOG1FYm1LOENWbE1ocTlOTk9GWk1DdG5odTY0N2xZK1phYkJVWXI0YlNnUGlK"
    "eHd3TW9yM2MxNVBGeC9kZVpBWWVBdGJWOXpXMFEwN3lYbWpPb1FodGd2SmpFTzlwd3h3ZjFn"
    "a3REOVdiajdPcFNpTE5sS0dwTEZPVGptMGNreklCR2d3dllXcCtBNkxDam1PenVWOTFoZFVG"
    "NExFckcwWjZHUVZsbGF6SFNKNW9hTkVKeDZ3eUpudCtnTDRURFh3Z0RGN1Fwa1NpeEJnZng1"
    "VFk5UVZzVGkvd0x6a0RDamw4eHVYM1lYZGxvam9ma3N4YTgzTUFGNlc4UHVhNFpoS0ZUY25H"
    "QUZRTVRmUE1VdDBCQUVreVR4bEFvdlo3SCtaWENrRDQ3VGtjR0k5S1dhdjdkREw5UDRJcVFs"
    "akQ5ZnIvUjBhbmxIK3J3Sm45akoxVXFUYldvSGdZcjhxTmE0U2tEM1dmWmhiN1RRSmJVRDZW"
    "b2NyRXFCejZQOVdnSkZsQjBObjU0dWU3UmxGQzUrbmxWOG02WlBiZjYrZjd3Vk9yVm4wT2J4"
    "cTJ0OVJTaUw5QWViUERnZnRzK0pndmZsbVBTT0hENVcrNG80MlM0L2h1ZWxmRnh1SU0xYWlk"
    "OGxaaXAwVEpCellYV21PQ3AyU1BIZE4wd0lwNy9tMUZqSjVaN3JqcW4wZEIrb1h2SGFweXdB"
    "ZHltRWFWbS9yczk0MGQ1MGNHZy8xUmZ2QUMzb1lTeVplOTlZZUs5REVRbzEyNDkrMG42UWho"
    "b0pRSkFDdz09PC9jZXJ0PgogIDwvaW50ZXJtZWRpYXRlcz4KICA8Y2VydGlmaWNhdGU+TUlJ"
    "RkdUQ0NBd0dnQXdJQkFnSVJBUExPcU90NFFLeHFDakV5b09zZTJpMHdEUVlKS29aSWh2Y05B"
    "UUVMQlFBd09URTNNRFVHQTFVRUF4TXVSMjl2WjJ4bElFTnNiM1ZrSUV0bGVTQldZWFZzZENC"
    "VFpYSjJhV05sSUVsdWRHVnliV1ZrYVdGMFpTQkRRVEFlRncweU16QTFNRGd5TVRJM01UaGFG"
    "dzB5T0RBMU1Ea3lNVEkzTVRoYU1EVXhNekF4QmdOVkJBTVRLa2R2YjJkc1pTQkRiRzkxWkNC"
    "TFpYa2dWbUYxYkhRZ1UyVnlkbWxqWlNCVGFXZHVhVzVuSUV0bGVUQ0NBaUl3RFFZSktvWklo"
    "dmNOQVFFQkJRQURnZ0lQQURDQ0Fnb0NnZ0lCQU0wMEYyT0xTRnJRVmVwM1ZrdVJWNXhlSnlX"
    "bGZiZ2pGa0NLQVRsR0EyaTNkNnJWRlViRzlSSm9WSGtOMkE1YXJVZUc2aTFhVnJ4YUM1VVBn"
    "OGRVWWcvbkxxSHNYY0NXd1NjT202MlBJV0Y3b2t6YkhkOFFWQ0R5WGpHR01VSWhvYzFOcU5W"
    "b1RTeTFhSFBmc0pabE9yUzNQTTRoYnJ1VU1HdGZnVkl3SWJxZUUrOXkzMzZ3QTB0VVFFMytl"
    "UEZtOThUdW51RFJGR1lJbHZ1ZEFFSHY4bElwenMvOFFYM1lOeHRxelJEMUJjQ3YzeEhCbkVZ"
    "MWFnMFBJakZ0SW12MDlLYm9maEtOemRzK1VOaWhQQUszelBqdHFhbHBoWS9zQlhmOXJ3UTY2"
    "UGJXYVlvcmpmVms4UXM2OC9NK1pYdXVRVnpPcENvOFR3M2lnWlpaTmx2YmVPb2pOeG9sdi8x"
    "eXNMV2VzNXAxUXpHdVk1N0tsK2syaDBzcVgyZVgwR3RFeXNRVi9GdURwVDFyM2tGV3dRaTMv"
    "Ukg0RXAwbWdteTZBdWVNd0FJbU16Vkl3YWx2REY1NFVHUUo4VlRyT1UzRERNU1Zyc253eTBZ"
    "QzJRUWFMdzQ2UmxQUm5NN296WUZJVUppZFpQd1dBUUpYb1F3NzVqempKajBZUi96dm8yd1ZE"
    "MzBSc2ttRnpkcUNJTXhNWjBQOFNhSUhIUVFWcU9FaGdTWis3VVJ0NmVzRmd4UlZYcU0zWVNr"
    "V1VkdEwwMHI3UEVwTkdIV1piNTNiYlJ5Tlh5UUhqeXRIcHdTVTF4OUN2ZDhoZ2xXMG11elFp"
    "NVQ2ZVQwVVRCUWVYa2I0YjF6dnlGbCtIOWtZbWN1UGkwWDJia21Ic3JwVkJ5V2ZMclc1Yjlt"
    "a3FSWjBZRjU3QWdNQkFBR2pJREFlTUE0R0ExVWREd0VCL3dRRUF3SUhnREFNQmdOVkhSTUJB"
    "ZjhFQWpBQU1BMEdDU3FHU0liM0RRRUJDd1VBQTRJQ0FRQnVZaGQ0VEtJREVNRXdUOExvTXgw"
    "NDBvbG9GR2hIV1NOVzJGSFdCazNVbVJJS2VsbFRVajJzT1l4OW5ReVVhMm1uNW9WdFNvZzRS"
    "SE1vTUh1YjlRWjVpZ2xUamlZUCtoTWxXK2lMRDhNNExxSHU1bW1Cd3hqOGI5WExWanVydk9R"
    "SFgzMG9ONklscFVGckdhVTBVTXp0SVhBNUtpOU4xdFJ3dm15VU9zbnpFR20rWkkrWWdJdU9B"
    "dk1LaUZRSGszM2U5QzFuTzNZcnViVmV4Z1VWUEJDbHpKUGFvWjI4YytDT2duaXJzemtOTk1X"
    "cHZFK25uVVNNbGlFV3V2TmorQmhldEFHNnNDNjJXT2xZOE41ZSt1UkVKSjY1MXg5Uy9NaG9T"
    "bWd5emM2d0RyRXJ2NC9ENEFtN1I2NWNCVURDbi9hMGZDSEVQMkMyeCt2WDBrdzE5MVh2ZlFE"
    "VzgzcytKWWUwVUdaT0ZxLzh3a3B1MWkzTzFZV1FwZHJxMzE2MFcyVmNOWWpOVERaVDlZRzMz"
    "OGZKK25NbmhTUEFCMllmRmdDb202QTZ0UC9kdzFodWg3SER4eUM0cm5sOERpcjhXQnZmQVg3"
    "Ujh5RnBOSStmS1BDUWpReXI5UEdVUUk2bmlIQWVhQWo1bklBalpaeXRacE1mLzYwV2lmd0hS"
    "UEJwdURvTUdxc3QxSzFzei8xQ0dXQ2dsSC81Vk5rSVQvKzhlRENOb1B5RlhRRGtJMFVRV0p0"
    "dDZ3UEVVdmJ0MHU2dTdUb3l1NkI2UE9DdjRHdWYyU09xTXZhdUVoWWZlT2hpdjUrYnU2NVRS"
    "TUxpMFZnNjZTZGNjd1pIdzRNbURocFNzSjY0UmdrL0UzbzBoUU9UcWZYU3ZIT1BxT1ZaWTBL"
    "L282dWNsNEluaWUrdGZnPT08L2NlcnRpZmljYXRlPgogIDx2YWx1ZT5SK1UvYVdBaUxqODNh"
    "aWN2MlN3Ui96ZDBXeVBPYVNNeWg3R21UcHg0ZmNabW4xWFYwVncyR0Y2Q2tTNGx5L1ZuKys3"
    "S0dxL0oxM1JwaHl2N1lDUGxWSjlZaUg2enVqQnE4Q1NYMi9CV1FqaFFRbHhzb0JEV0NieUdp"
    "bUorYnJpc0pNUEFrYjNmV1paamZMZDFIUlQ0YUNRK05JYThCWUNmcnM4RjNIaVZYcG9DcFdr"
    "SWU3Sk1rK2lUd0U1M3E2OVpyVkpCdEFQVVNLenU4R1dEVGt1blBvK3lsUGpUT3ArRWN1c29S"
    "SStEUTVOcHZZdVk1dXp1OWRKM2s4R1dWK0lXMldvRTR1RWk0Y3FVeXY1NkNqSVVVMzlLNFRK"
    "WXVvMEY4emZOWStNcitmdnR0UG8vd2dXVUg0L0xrUGpkdXFrN3kvRU94c1pVTmJtZjVubERD"
    "RXFncVAvWlNoZ1d1cDA2ZDZDTEdzM3dVdVVQdUw3aGFOdFpwN0FtbXE4dHc0OCswajVhdmpL"
    "ZGZqNisrUjl0UEZoWWt2N0J2U2ZOVlliQVpKOFpQYWZRL2E2UDN3YVhpakZUVUppTEdOYnFB"
    "QjZjUFRPRnZFUlRLenZ3RGQ0NGtNUkVYUFhRQkU2dXdsTVZBaVAxNURjM2owOUNRQXBDR1NZ"
    "dTBvV1dQZFZESldIclFlTmc2ckNZM0dJdnFuOFdsYTZVVzIxVE16dUNWVkZHYk50WExRbHJn"
    "WDdmRmE1cWJLckVsZHhlbkZOczFPMUdCMWovVlVxdXN4RGcvWXB0TUs3RHBiQld1S0N6cEo3"
    "VGpCVUhUM1JjWUpWNDB1aFI3L0dFZDdwZ0x6ak1ndWRIc3VKTy8zRTN6UlliZHNZY2FqeG9S"
    "NjdvdDhHeG9scjZkR3NPcHMzMWYvRT08L3ZhbHVlPgo8L3NpZ25hdHVyZT4=";

TEST(RecoverableKeyStoreBackendCertVerifyTest, Success) {
  std::string cert_xml, sig_xml;
  ASSERT_TRUE(base::Base64Decode(kCertXmlB64, &cert_xml));
  ASSERT_TRUE(base::Base64Decode(kSigXmlB64, &sig_xml));
  std::optional<RecoverableKeyStoreCertList> cert_list =
      VerifyAndParseRecoverableKeyStoreBackendCertXmls(cert_xml, sig_xml);
  ASSERT_TRUE(cert_list.has_value());
  EXPECT_EQ(cert_list->version, 10014);
  // SecureBox-encoded public key size is 65.
  EXPECT_THAT(cert_list->certs,
              Each(Field(&RecoverableKeyStoreCert::public_key, SizeIs(65))));
}

// TODO(b/309734008): For those failure tests to be more useful, use mock
// metrics expectations after we added metrics reporting.

TEST(RecoverableKeyStoreBackendCertVerifyTest, FailNotXml) {
  const std::string sig_xml = "not a xml";
  std::string cert_xml;
  ASSERT_TRUE(base::Base64Decode(kCertXmlB64, &cert_xml));
  EXPECT_FALSE(
      VerifyAndParseRecoverableKeyStoreBackendCertXmls(cert_xml, sig_xml)
          .has_value());
}

TEST(RecoverableKeyStoreBackendCertVerifyTest, FailSignatureXmlVerification) {
  std::string cert_xml, sig_xml;
  ASSERT_TRUE(base::Base64Decode(kCertXmlB64, &cert_xml));
  ASSERT_TRUE(base::Base64Decode(kInvalidSigXmlB64, &sig_xml));
  EXPECT_FALSE(
      VerifyAndParseRecoverableKeyStoreBackendCertXmls(cert_xml, sig_xml)
          .has_value());
}

TEST(RecoverableKeyStoreBackendCertVerifyTest, FailCertXmlVerification) {
  std::string cert_xml, sig_xml;
  ASSERT_TRUE(base::Base64Decode(kCertXmlB64, &cert_xml));
  ASSERT_TRUE(base::Base64Decode(kOldSigXmlB64, &sig_xml));
  EXPECT_FALSE(
      VerifyAndParseRecoverableKeyStoreBackendCertXmls(cert_xml, sig_xml)
          .has_value());
}

// Practically, if the signature verification of the xml file succeeds, the
// remaining steps will likely succeed too. Those code paths are harder to test
// because the server never signed an invalid cert xml.

}  // namespace
}  // namespace cryptohome
