#pragma once

#include <string>
#include <variant>

struct DTOCourseList {};

struct DTOCourseWorksList {
    std::string courseId;
};

struct DTOStudentsList {
    std::string courseId;
};

struct DTOStudentWorksDownload {
    std::string fileId;
};

struct DTOUserInfo {};

struct DTOStudentWorksData {
    std::string courseId;
    std::string courseWorkId;
};

struct DTOError {
    std::string errorMessage;
};

using DTOCreateRequest = std::variant<
    DTOCourseList, DTOCourseWorksList, DTOStudentsList, DTOStudentWorksDownload, DTOError, DTOStudentWorksData>;