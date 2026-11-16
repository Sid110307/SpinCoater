package com.sid.spincoater.core

import androidx.room.Dao
import androidx.room.Delete
import androidx.room.Embedded
import androidx.room.Entity
import androidx.room.ForeignKey
import androidx.room.Index
import androidx.room.Insert
import androidx.room.PrimaryKey
import androidx.room.Query
import androidx.room.Relation
import androidx.room.Transaction
import androidx.room.Update

@Entity("recipes")
data class Recipe(
    @PrimaryKey(autoGenerate = true) val id: Long = 0L,
    val name: String,
    val notes: String = "",
    val createdAt: Long = System.currentTimeMillis()
)

@Entity(
    tableName = "recipe_steps",
    foreignKeys = [ForeignKey(
        entity = Recipe::class,
        parentColumns = ["id"],
        childColumns = ["recipeId"],
        onDelete = ForeignKey.CASCADE
    )],
    indices = [Index("recipeId")]
)
data class RecipeStep(
    @PrimaryKey(autoGenerate = true) val id: Long = 0L,
    val recipeId: Long,
    val position: Int,
    val rpm: Int,
    val accelMs: Int,
    val dwellMs: Int,
    val clockwise: Boolean = true
)

data class RecipeWithSteps(
    @Embedded val recipe: Recipe,
    @Relation(parentColumn = "id", entityColumn = "recipeId")
    val steps: List<RecipeStep>
) {
    val ordered: List<RecipeStep> get() = steps.sortedBy { it.position }
    val totalMs: Int get() = steps.sumOf { it.accelMs + it.dwellMs }
}

@Dao
interface RecipeDao {
    @Transaction
    @Query("SELECT * FROM recipes ORDER BY createdAt DESC")
    suspend fun all(): List<RecipeWithSteps>

    @Transaction
    @Query("SELECT * FROM recipes WHERE id = :id")
    suspend fun byId(id: Long): RecipeWithSteps?

    @Insert
    suspend fun insertRecipe(recipe: Recipe): Long

    @Update
    suspend fun updateRecipe(recipe: Recipe)

    @Delete
    suspend fun deleteRecipe(recipe: Recipe)

    @Insert
    suspend fun insertSteps(steps: List<RecipeStep>)

    @Query("DELETE FROM recipe_steps WHERE recipeId = :recipeId")
    suspend fun clearSteps(recipeId: Long)
}

class RecipeRepo(private val dao: RecipeDao) {
    suspend fun list() = dao.all()
    suspend fun get(id: Long) = dao.byId(id)

    suspend fun save(
        existingId: Long?,
        name: String,
        notes: String,
        steps: List<RecipeStep>
    ): Long {
        val id = if (existingId == null || existingId == 0L) {
            dao.insertRecipe(Recipe(name = name, notes = notes))
        } else {
            val current = dao.byId(existingId)?.recipe ?: Recipe(id = existingId, name = name)

            dao.updateRecipe(current.copy(name = name, notes = notes))
            dao.clearSteps(existingId)
            existingId
        }

        dao.insertSteps(steps.mapIndexed { index, step ->
            step.copy(id = 0L, recipeId = id, position = index)
        })
        return id
    }

    suspend fun delete(recipe: Recipe) = dao.deleteRecipe(recipe)
}
