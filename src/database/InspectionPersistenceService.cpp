#include "database/InspectionPersistenceService.hpp"

#include "database/DatabaseTransaction.hpp"
#include "database/DefectRepository.hpp"
#include "database/InspectionImageRepository.hpp"
#include "database/InspectionRepository.hpp"

namespace optinspect::database {

std::int64_t InspectionPersistenceService::persist(const InspectionPersistenceRequest& request) {
    Transaction transaction(db_);

    InspectionRepository inspections(db_);
    DefectRepository defects(db_);
    InspectionImageRepository images(db_);

    const std::int64_t inspection_id = inspections.create(request.inspection);
    defects.createMany(inspection_id, request.defects);
    for (InspectionImage image : request.images) {
        image.inspection_id = inspection_id;
        images.create(image);
    }

    transaction.commit();
    return inspection_id;
}

bool InspectionPersistenceService::deleteInspection(std::int64_t inspection_id) {
    Transaction transaction(db_);
    InspectionRepository inspections(db_);
    const bool deleted = inspections.remove(inspection_id);
    transaction.commit();
    return deleted;
}

}  // namespace optinspect::database
